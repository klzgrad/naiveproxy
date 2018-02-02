// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/extras/sqlite/sqlite_persistent_cookie_store.h"

#include <iterator>
#include <map>
#include <memory>
#include <set>

#include "base/bind.h"
#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram_macros.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/time/time.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_util.h"
#include "net/extras/sqlite/cookie_crypto_delegate.h"
#include "sql/error_delegate_util.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"
#include "url/gurl.h"

using base::Time;

namespace {

// The persistent cookie store is loaded into memory on eTLD at a time. This
// variable controls the delay between loading eTLDs, so as to not overload the
// CPU or I/O with these low priority requests immediately after start up.
#if defined(OS_IOS)
// TODO(ellyjones): This should be 200ms, but currently CookieStoreIOS is
// waiting for -FinishedLoadingCookies to be called after all eTLD cookies are
// loaded before making any network requests.  Changing to 0ms for now.
// crbug.com/462593
const int kLoadDelayMilliseconds = 0;
#else
const int kLoadDelayMilliseconds = 0;
#endif

}  // namespace

namespace net {

// This class is designed to be shared between any client thread and the
// background task runner. It batches operations and commits them on a timer.
//
// SQLitePersistentCookieStore::Load is called to load all cookies.  It
// delegates to Backend::Load, which posts a Backend::LoadAndNotifyOnDBThread
// task to the background runner.  This task calls Backend::ChainLoadCookies(),
// which repeatedly posts itself to the BG runner to load each eTLD+1's cookies
// in separate tasks.  When this is complete, Backend::CompleteLoadOnIOThread is
// posted to the client runner, which notifies the caller of
// SQLitePersistentCookieStore::Load that the load is complete.
//
// If a priority load request is invoked via SQLitePersistentCookieStore::
// LoadCookiesForKey, it is delegated to Backend::LoadCookiesForKey, which posts
// Backend::LoadKeyAndNotifyOnDBThread to the BG runner. That routine loads just
// that single domain key (eTLD+1)'s cookies, and posts a Backend::
// CompleteLoadForKeyOnIOThread to the client runner to notify the caller of
// SQLitePersistentCookieStore::LoadCookiesForKey that that load is complete.
//
// Subsequent to loading, mutations may be queued by any thread using
// AddCookie, UpdateCookieAccessTime, and DeleteCookie. These are flushed to
// disk on the BG runner every 30 seconds, 512 operations, or call to Flush(),
// whichever occurs first.
class SQLitePersistentCookieStore::Backend
    : public base::RefCountedThreadSafe<SQLitePersistentCookieStore::Backend> {
 public:
  Backend(
      const base::FilePath& path,
      const scoped_refptr<base::SequencedTaskRunner>& client_task_runner,
      const scoped_refptr<base::SequencedTaskRunner>& background_task_runner,
      bool restore_old_session_cookies,
      CookieCryptoDelegate* crypto_delegate)
      : path_(path),
        num_pending_(0),
        initialized_(false),
        corruption_detected_(false),
        restore_old_session_cookies_(restore_old_session_cookies),
        num_cookies_read_(0),
        client_task_runner_(client_task_runner),
        background_task_runner_(background_task_runner),
        num_priority_waiting_(0),
        total_priority_requests_(0),
        crypto_(crypto_delegate) {}

  // Creates or loads the SQLite database.
  void Load(const LoadedCallback& loaded_callback);

  // Loads cookies for the domain key (eTLD+1).
  void LoadCookiesForKey(const std::string& domain,
                         const LoadedCallback& loaded_callback);

  // Steps through all results of |smt|, makes a cookie from each, and adds the
  // cookie to |cookies|. This method also updates |num_cookies_read_|.
  void MakeCookiesFromSQLStatement(
      std::vector<std::unique_ptr<CanonicalCookie>>* cookies,
      sql::Statement* statement);

  // Batch a cookie addition.
  void AddCookie(const CanonicalCookie& cc);

  // Batch a cookie access time update.
  void UpdateCookieAccessTime(const CanonicalCookie& cc);

  // Batch a cookie deletion.
  void DeleteCookie(const CanonicalCookie& cc);

  // Sets callback to run at the beginning of Commit.
  void SetBeforeFlushCallback(base::RepeatingClosure callback);

  // Commit pending operations as soon as possible.
  void Flush(base::OnceClosure callback);

  // Commit any pending operations and close the database.  This must be called
  // before the object is destructed.
  void Close(const base::Closure& callback);

  // Post background delete of all cookies that match |cookies|.
  void DeleteAllInList(const std::list<CookieOrigin>& cookies);

 private:
  friend class base::RefCountedThreadSafe<SQLitePersistentCookieStore::Backend>;

  // You should call Close() before destructing this object.
  ~Backend() {
    DCHECK(!db_.get()) << "Close should have already been called.";
    DCHECK_EQ(0u, num_pending_);
    DCHECK(pending_.empty());
  }

  // Database upgrade statements.
  bool EnsureDatabaseVersion();

  class PendingOperation {
   public:
    enum OperationType {
      COOKIE_ADD,
      COOKIE_UPDATEACCESS,
      COOKIE_DELETE,
    };

    PendingOperation(OperationType op, const CanonicalCookie& cc)
        : op_(op), cc_(cc) {}

    OperationType op() const { return op_; }
    const CanonicalCookie& cc() const { return cc_; }

   private:
    OperationType op_;
    CanonicalCookie cc_;
  };

 private:
  // Creates or loads the SQLite database on background runner.
  void LoadAndNotifyInBackground(const LoadedCallback& loaded_callback,
                                 const base::Time& posted_at);

  // Loads cookies for the domain key (eTLD+1) on background runner.
  void LoadKeyAndNotifyInBackground(const std::string& domains,
                                    const LoadedCallback& loaded_callback,
                                    const base::Time& posted_at);

  // Notifies the CookieMonster when loading completes for a specific domain key
  // or for all domain keys. Triggers the callback and passes it all cookies
  // that have been loaded from DB since last IO notification.
  void Notify(const LoadedCallback& loaded_callback, bool load_success);

  // Flushes (Commits) pending operations on the background runner, and invokes
  // |callback| on the client thread when done.
  void FlushAndNotifyInBackground(base::OnceClosure callback);

  // Sends notification when the entire store is loaded, and reports metrics
  // for the total time to load and aggregated results from any priority loads
  // that occurred.
  void CompleteLoadInForeground(const LoadedCallback& loaded_callback,
                                bool load_success);

  // Sends notification when a single priority load completes. Updates priority
  // load metric data. The data is sent only after the final load completes.
  void CompleteLoadForKeyInForeground(const LoadedCallback& loaded_callback,
                                      bool load_success,
                                      const base::Time& requested_at);

  // Sends all metrics, including posting a ReportMetricsInBackground task.
  // Called after all priority and regular loading is complete.
  void ReportMetrics();

  // Sends background-runner owned metrics (i.e., the combined duration of all
  // BG-runner tasks).
  void ReportMetricsInBackground();

  // Initialize the data base.
  bool InitializeDatabase();

  // Loads cookies for the next domain key from the DB, then either reschedules
  // itself or schedules the provided callback to run on the client runner (if
  // all domains are loaded).
  void ChainLoadCookies(const LoadedCallback& loaded_callback);

  // Load all cookies for a set of domains/hosts
  bool LoadCookiesForDomains(const std::set<std::string>& key);

  // Batch a cookie operation (add or delete)
  void BatchOperation(PendingOperation::OperationType op,
                      const CanonicalCookie& cc);
  // Commit our pending operations to the database.
  void Commit();
  // Close() executed on the background runner.
  void InternalBackgroundClose(const base::Closure& callback);

  void DeleteSessionCookiesOnStartup();

  void BackgroundDeleteAllInList(const std::list<CookieOrigin>& cookies);

  void DatabaseErrorCallback(int error, sql::Statement* stmt);
  void KillDatabase();

  void PostBackgroundTask(const base::Location& origin, base::OnceClosure task);
  void PostClientTask(const base::Location& origin, base::OnceClosure task);

  // Shared code between the different load strategies to be used after all
  // cookies have been loaded.
  void FinishedLoadingCookies(const LoadedCallback& loaded_callback,
                              bool success);

  const base::FilePath path_;
  std::unique_ptr<sql::Connection> db_;
  sql::MetaTable meta_table_;

  typedef std::list<PendingOperation*> PendingOperationsList;
  PendingOperationsList pending_;
  PendingOperationsList::size_type num_pending_;
  // Guard |cookies_|, |pending_|, |num_pending_|.
  base::Lock lock_;

  // Temporary buffer for cookies loaded from DB. Accumulates cookies to reduce
  // the number of messages sent to the client runner. Sent back in response to
  // individual load requests for domain keys or when all loading completes.
  std::vector<std::unique_ptr<CanonicalCookie>> cookies_;

  // Map of domain keys(eTLD+1) to domains/hosts that are to be loaded from DB.
  std::map<std::string, std::set<std::string>> keys_to_load_;

  // Indicates if DB has been initialized.
  bool initialized_;

  // Indicates if the kill-database callback has been scheduled.
  bool corruption_detected_;

  // If false, we should filter out session cookies when reading the DB.
  bool restore_old_session_cookies_;

  // The cumulative time spent loading the cookies on the background runner.
  // Incremented and reported from the background runner.
  base::TimeDelta cookie_load_duration_;

  // The total number of cookies read. Incremented and reported on the
  // background runner.
  int num_cookies_read_;

  scoped_refptr<base::SequencedTaskRunner> client_task_runner_;
  scoped_refptr<base::SequencedTaskRunner> background_task_runner_;

  // Guards the following metrics-related properties (only accessed when
  // starting/completing priority loads or completing the total load).
  base::Lock metrics_lock_;
  int num_priority_waiting_;
  // The total number of priority requests.
  int total_priority_requests_;
  // The time when |num_priority_waiting_| incremented to 1.
  base::Time current_priority_wait_start_;
  // The cumulative duration of time when |num_priority_waiting_| was greater
  // than 1.
  base::TimeDelta priority_wait_duration_;
  // Class with functions that do cryptographic operations (for protecting
  // cookies stored persistently).
  //
  // Not owned.
  CookieCryptoDelegate* crypto_;
  // Callback to run before Commit.
  base::RepeatingClosure before_flush_callback_;
  // Guards |before_flush_callback_|.
  base::Lock before_flush_callback_lock_;

  DISALLOW_COPY_AND_ASSIGN(Backend);
};

namespace {

// Version number of the database.
//
// Version 9 adds a partial index to track non-persistent cookies.
// Non-persistent cookies sometimes need to be deleted on startup. There are
// frequently few or no non-persistent cookies, so the partial index allows the
// deletion to be sped up or skipped, without having to page in the DB.
//
// Version 8 adds "first-party only" cookies.
//
// Version 7 adds encrypted values.  Old values will continue to be used but
// all new values written will be encrypted on selected operating systems.  New
// records read by old clients will simply get an empty cookie value while old
// records read by new clients will continue to operate with the unencrypted
// version.  New and old clients alike will always write/update records with
// what they support.
//
// Version 6 adds cookie priorities. This allows developers to influence the
// order in which cookies are evicted in order to meet domain cookie limits.
//
// Version 5 adds the columns has_expires and is_persistent, so that the
// database can store session cookies as well as persistent cookies. Databases
// of version 5 are incompatible with older versions of code. If a database of
// version 5 is read by older code, session cookies will be treated as normal
// cookies. Currently, these fields are written, but not read anymore.
//
// In version 4, we migrated the time epoch.  If you open the DB with an older
// version on Mac or Linux, the times will look wonky, but the file will likely
// be usable. On Windows version 3 and 4 are the same.
//
// Version 3 updated the database to include the last access time, so we can
// expire them in decreasing order of use when we've reached the maximum
// number of cookies.
const int kCurrentVersionNumber = 9;
const int kCompatibleVersionNumber = 5;

// Possible values for the 'priority' column.
enum DBCookiePriority {
  kCookiePriorityLow = 0,
  kCookiePriorityMedium = 1,
  kCookiePriorityHigh = 2,
};

DBCookiePriority CookiePriorityToDBCookiePriority(CookiePriority value) {
  switch (value) {
    case COOKIE_PRIORITY_LOW:
      return kCookiePriorityLow;
    case COOKIE_PRIORITY_MEDIUM:
      return kCookiePriorityMedium;
    case COOKIE_PRIORITY_HIGH:
      return kCookiePriorityHigh;
  }

  NOTREACHED();
  return kCookiePriorityMedium;
}

CookiePriority DBCookiePriorityToCookiePriority(DBCookiePriority value) {
  switch (value) {
    case kCookiePriorityLow:
      return COOKIE_PRIORITY_LOW;
    case kCookiePriorityMedium:
      return COOKIE_PRIORITY_MEDIUM;
    case kCookiePriorityHigh:
      return COOKIE_PRIORITY_HIGH;
  }

  NOTREACHED();
  return COOKIE_PRIORITY_DEFAULT;
}

// Possible values for the 'samesite' column
enum DBCookieSameSite {
  kCookieSameSiteNoRestriction = 0,
  kCookieSameSiteLax = 1,
  kCookieSameSiteStrict = 2,
};

DBCookieSameSite CookieSameSiteToDBCookieSameSite(CookieSameSite value) {
  switch (value) {
    case CookieSameSite::NO_RESTRICTION:
      return kCookieSameSiteNoRestriction;
    case CookieSameSite::LAX_MODE:
      return kCookieSameSiteLax;
    case CookieSameSite::STRICT_MODE:
      return kCookieSameSiteStrict;
  }

  NOTREACHED();
  return kCookieSameSiteNoRestriction;
}

CookieSameSite DBCookieSameSiteToCookieSameSite(DBCookieSameSite value) {
  switch (value) {
    case kCookieSameSiteNoRestriction:
      return CookieSameSite::NO_RESTRICTION;
    case kCookieSameSiteLax:
      return CookieSameSite::LAX_MODE;
    case kCookieSameSiteStrict:
      return CookieSameSite::STRICT_MODE;
  }

  NOTREACHED();
  return CookieSameSite::DEFAULT_MODE;
}

// Increments a specified TimeDelta by the duration between this object's
// constructor and destructor. Not thread safe. Multiple instances may be
// created with the same delta instance as long as their lifetimes are nested.
// The shortest lived instances have no impact.
class IncrementTimeDelta {
 public:
  explicit IncrementTimeDelta(base::TimeDelta* delta)
      : delta_(delta), original_value_(*delta), start_(base::Time::Now()) {}

  ~IncrementTimeDelta() {
    *delta_ = original_value_ + base::Time::Now() - start_;
  }

 private:
  base::TimeDelta* delta_;
  base::TimeDelta original_value_;
  base::Time start_;

  DISALLOW_COPY_AND_ASSIGN(IncrementTimeDelta);
};

// Initializes the cookies table, returning true on success.
bool InitTable(sql::Connection* db) {
  if (db->DoesTableExist("cookies"))
    return true;

  std::string stmt(base::StringPrintf(
      "CREATE TABLE cookies ("
      "creation_utc INTEGER NOT NULL UNIQUE PRIMARY KEY,"
      "host_key TEXT NOT NULL,"
      "name TEXT NOT NULL,"
      "value TEXT NOT NULL,"
      "path TEXT NOT NULL,"
      "expires_utc INTEGER NOT NULL,"
      "secure INTEGER NOT NULL,"
      "httponly INTEGER NOT NULL,"
      "last_access_utc INTEGER NOT NULL, "
      "has_expires INTEGER NOT NULL DEFAULT 1, "
      "persistent INTEGER NOT NULL DEFAULT 1,"
      "priority INTEGER NOT NULL DEFAULT %d,"
      "encrypted_value BLOB DEFAULT '',"
      "firstpartyonly INTEGER NOT NULL DEFAULT %d)",
      CookiePriorityToDBCookiePriority(COOKIE_PRIORITY_DEFAULT),
      CookieSameSiteToDBCookieSameSite(CookieSameSite::DEFAULT_MODE)));
  if (!db->Execute(stmt.c_str()))
    return false;

  if (!db->Execute("CREATE INDEX domain ON cookies(host_key)"))
    return false;

#if defined(OS_IOS)
  // iOS 8.1 and older doesn't support partial indices. iOS 8.2 supports
  // partial indices.
  if (!db->Execute("CREATE INDEX is_transient ON cookies(persistent)")) {
#else
  if (!db->Execute(
          "CREATE INDEX is_transient ON cookies(persistent) "
          "where persistent != 1")) {
#endif
    return false;
  }

  return true;
}

}  // namespace

void SQLitePersistentCookieStore::Backend::Load(
    const LoadedCallback& loaded_callback) {
  PostBackgroundTask(FROM_HERE,
                     base::Bind(&Backend::LoadAndNotifyInBackground, this,
                                loaded_callback, base::Time::Now()));
}

void SQLitePersistentCookieStore::Backend::LoadCookiesForKey(
    const std::string& key,
    const LoadedCallback& loaded_callback) {
  {
    base::AutoLock locked(metrics_lock_);
    if (num_priority_waiting_ == 0)
      current_priority_wait_start_ = base::Time::Now();
    num_priority_waiting_++;
    total_priority_requests_++;
  }

  PostBackgroundTask(
      FROM_HERE, base::Bind(&Backend::LoadKeyAndNotifyInBackground, this, key,
                            loaded_callback, base::Time::Now()));
}

void SQLitePersistentCookieStore::Backend::LoadAndNotifyInBackground(
    const LoadedCallback& loaded_callback,
    const base::Time& posted_at) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  IncrementTimeDelta increment(&cookie_load_duration_);

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeLoadDBQueueWait",
                             base::Time::Now() - posted_at,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  if (!InitializeDatabase()) {
    PostClientTask(FROM_HERE, base::Bind(&Backend::CompleteLoadInForeground,
                                         this, loaded_callback, false));
  } else {
    ChainLoadCookies(loaded_callback);
  }
}

void SQLitePersistentCookieStore::Backend::LoadKeyAndNotifyInBackground(
    const std::string& key,
    const LoadedCallback& loaded_callback,
    const base::Time& posted_at) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  IncrementTimeDelta increment(&cookie_load_duration_);

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeKeyLoadDBQueueWait",
                             base::Time::Now() - posted_at,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  bool success = false;
  if (InitializeDatabase()) {
    std::map<std::string, std::set<std::string>>::iterator it =
        keys_to_load_.find(key);
    if (it != keys_to_load_.end()) {
      success = LoadCookiesForDomains(it->second);
      keys_to_load_.erase(it);
    } else {
      success = true;
    }
  }

  PostClientTask(
      FROM_HERE,
      base::Bind(
          &SQLitePersistentCookieStore::Backend::CompleteLoadForKeyInForeground,
          this, loaded_callback, success, posted_at));
}

void SQLitePersistentCookieStore::Backend::FlushAndNotifyInBackground(
    base::OnceClosure callback) {
  Commit();
  if (!callback.is_null())
    PostClientTask(FROM_HERE, std::move(callback));
}

void SQLitePersistentCookieStore::Backend::CompleteLoadForKeyInForeground(
    const LoadedCallback& loaded_callback,
    bool load_success,
    const ::Time& requested_at) {
  DCHECK(client_task_runner_->RunsTasksInCurrentSequence());

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeKeyLoadTotalWait",
                             base::Time::Now() - requested_at,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  Notify(loaded_callback, load_success);

  {
    base::AutoLock locked(metrics_lock_);
    num_priority_waiting_--;
    if (num_priority_waiting_ == 0) {
      priority_wait_duration_ +=
          base::Time::Now() - current_priority_wait_start_;
    }
  }
}

void SQLitePersistentCookieStore::Backend::ReportMetricsInBackground() {
  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeLoad", cookie_load_duration_,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);
}

void SQLitePersistentCookieStore::Backend::ReportMetrics() {
  PostBackgroundTask(
      FROM_HERE,
      base::Bind(
          &SQLitePersistentCookieStore::Backend::ReportMetricsInBackground,
          this));

  {
    base::AutoLock locked(metrics_lock_);
    UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.PriorityBlockingTime",
                               priority_wait_duration_,
                               base::TimeDelta::FromMilliseconds(1),
                               base::TimeDelta::FromMinutes(1), 50);

    UMA_HISTOGRAM_COUNTS_100("Cookie.PriorityLoadCount",
                             total_priority_requests_);

    UMA_HISTOGRAM_COUNTS_10000("Cookie.NumberOfLoadedCookies",
                               num_cookies_read_);
  }
}

void SQLitePersistentCookieStore::Backend::CompleteLoadInForeground(
    const LoadedCallback& loaded_callback,
    bool load_success) {
  Notify(loaded_callback, load_success);

  if (load_success)
    ReportMetrics();
}

void SQLitePersistentCookieStore::Backend::Notify(
    const LoadedCallback& loaded_callback,
    bool load_success) {
  DCHECK(client_task_runner_->RunsTasksInCurrentSequence());

  std::vector<std::unique_ptr<CanonicalCookie>> cookies;
  {
    base::AutoLock locked(lock_);
    cookies.swap(cookies_);
  }

  loaded_callback.Run(std::move(cookies));
}

bool SQLitePersistentCookieStore::Backend::InitializeDatabase() {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  if (initialized_ || corruption_detected_) {
    // Return false if we were previously initialized but the DB has since been
    // closed, or if corruption caused a database reset during initialization.
    return db_ != NULL;
  }

  base::Time start = base::Time::Now();

  const base::FilePath dir = path_.DirName();
  if (!base::PathExists(dir) && !base::CreateDirectory(dir)) {
    return false;
  }

  int64_t db_size = 0;
  if (base::GetFileSize(path_, &db_size))
    UMA_HISTOGRAM_COUNTS_1M("Cookie.DBSizeInKB", db_size / 1024);

  db_.reset(new sql::Connection);
  db_->set_histogram_tag("Cookie");

  // Unretained to avoid a ref loop with |db_|.
  db_->set_error_callback(
      base::Bind(&SQLitePersistentCookieStore::Backend::DatabaseErrorCallback,
                 base::Unretained(this)));

  if (!db_->Open(path_)) {
    NOTREACHED() << "Unable to open cookie DB.";
    if (corruption_detected_)
      db_->Raze();
    meta_table_.Reset();
    db_.reset();
    return false;
  }

  if (!EnsureDatabaseVersion() || !InitTable(db_.get())) {
    NOTREACHED() << "Unable to open cookie DB.";
    if (corruption_detected_)
      db_->Raze();
    meta_table_.Reset();
    db_.reset();
    return false;
  }

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeInitializeDB",
                             base::Time::Now() - start,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  start = base::Time::Now();

  // Retrieve all the domains
  sql::Statement smt(
      db_->GetUniqueStatement("SELECT DISTINCT host_key FROM cookies"));

  if (!smt.is_valid()) {
    if (corruption_detected_)
      db_->Raze();
    meta_table_.Reset();
    db_.reset();
    return false;
  }

  std::vector<std::string> host_keys;
  while (smt.Step())
    host_keys.push_back(smt.ColumnString(0));

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeLoadDomains",
                             base::Time::Now() - start,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  base::Time start_parse = base::Time::Now();

  // Build a map of domain keys (always eTLD+1) to domains.
  for (size_t idx = 0; idx < host_keys.size(); ++idx) {
    const std::string& domain = host_keys[idx];
    std::string key = registry_controlled_domains::GetDomainAndRegistry(
        domain, registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);

    keys_to_load_[key].insert(domain);
  }

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeParseDomains",
                             base::Time::Now() - start_parse,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  UMA_HISTOGRAM_CUSTOM_TIMES("Cookie.TimeInitializeDomainMap",
                             base::Time::Now() - start,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromMinutes(1), 50);

  initialized_ = true;

  if (!restore_old_session_cookies_)
    DeleteSessionCookiesOnStartup();
  return true;
}

void SQLitePersistentCookieStore::Backend::ChainLoadCookies(
    const LoadedCallback& loaded_callback) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  IncrementTimeDelta increment(&cookie_load_duration_);

  bool load_success = true;

  if (!db_) {
    // Close() has been called on this store.
    load_success = false;
  } else if (keys_to_load_.size() > 0) {
    // Load cookies for the first domain key.
    std::map<std::string, std::set<std::string>>::iterator it =
        keys_to_load_.begin();
    load_success = LoadCookiesForDomains(it->second);
    keys_to_load_.erase(it);
  }

  // If load is successful and there are more domain keys to be loaded,
  // then post a background task to continue chain-load;
  // Otherwise notify on client runner.
  if (load_success && keys_to_load_.size() > 0) {
    bool success = background_task_runner_->PostDelayedTask(
        FROM_HERE,
        base::Bind(&Backend::ChainLoadCookies, this, loaded_callback),
        base::TimeDelta::FromMilliseconds(kLoadDelayMilliseconds));
    if (!success) {
      LOG(WARNING) << "Failed to post task from " << FROM_HERE.ToString()
                   << " to background_task_runner_.";
    }
  } else {
    FinishedLoadingCookies(loaded_callback, load_success);
  }
}

bool SQLitePersistentCookieStore::Backend::LoadCookiesForDomains(
    const std::set<std::string>& domains) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  sql::Statement smt;
  if (restore_old_session_cookies_) {
    smt.Assign(db_->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT creation_utc, host_key, name, value, encrypted_value, path, "
        "expires_utc, secure, httponly, firstpartyonly, last_access_utc, "
        "has_expires, persistent, priority FROM cookies WHERE host_key = ?"));
  } else {
    smt.Assign(db_->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT creation_utc, host_key, name, value, encrypted_value, path, "
        "expires_utc, secure, httponly, firstpartyonly, last_access_utc, "
        "has_expires, persistent, priority FROM cookies WHERE host_key = ? "
        "AND persistent = 1"));
  }
  if (!smt.is_valid()) {
    smt.Clear();  // Disconnect smt_ref from db_.
    meta_table_.Reset();
    db_.reset();
    return false;
  }

  std::vector<std::unique_ptr<CanonicalCookie>> cookies;
  std::set<std::string>::const_iterator it = domains.begin();
  for (; it != domains.end(); ++it) {
    smt.BindString(0, *it);
    MakeCookiesFromSQLStatement(&cookies, &smt);
    smt.Reset(true);
  }
  {
    base::AutoLock locked(lock_);
    std::move(cookies.begin(), cookies.end(), std::back_inserter(cookies_));
  }
  return true;
}

void SQLitePersistentCookieStore::Backend::MakeCookiesFromSQLStatement(
    std::vector<std::unique_ptr<CanonicalCookie>>* cookies,
    sql::Statement* statement) {
  sql::Statement& smt = *statement;
  while (smt.Step()) {
    std::string value;
    std::string encrypted_value = smt.ColumnString(4);
    if (!encrypted_value.empty() && crypto_) {
      if (!crypto_->DecryptString(encrypted_value, &value))
        continue;
    } else {
      value = smt.ColumnString(3);
    }
    std::unique_ptr<CanonicalCookie> cc(std::make_unique<CanonicalCookie>(
        smt.ColumnString(2),                           // name
        value,                                         // value
        smt.ColumnString(1),                           // domain
        smt.ColumnString(5),                           // path
        Time::FromInternalValue(smt.ColumnInt64(0)),   // creation_utc
        Time::FromInternalValue(smt.ColumnInt64(6)),   // expires_utc
        Time::FromInternalValue(smt.ColumnInt64(10)),  // last_access_utc
        smt.ColumnInt(7) != 0,                         // secure
        smt.ColumnInt(8) != 0,                         // http_only
        DBCookieSameSiteToCookieSameSite(
            static_cast<DBCookieSameSite>(smt.ColumnInt(9))),  // samesite
        DBCookiePriorityToCookiePriority(
            static_cast<DBCookiePriority>(smt.ColumnInt(13)))));  // priority
    DLOG_IF(WARNING, cc->CreationDate() > Time::Now())
        << L"CreationDate too recent";
    if (cc->IsCanonical())
      cookies->push_back(std::move(cc));
    ++num_cookies_read_;
  }
}

bool SQLitePersistentCookieStore::Backend::EnsureDatabaseVersion() {
  // Version check.
  if (!meta_table_.Init(db_.get(), kCurrentVersionNumber,
                        kCompatibleVersionNumber)) {
    return false;
  }

  if (meta_table_.GetCompatibleVersionNumber() > kCurrentVersionNumber) {
    LOG(WARNING) << "Cookie database is too new.";
    return false;
  }

  int cur_version = meta_table_.GetVersionNumber();
  if (cur_version == 2) {
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;
    if (!db_->Execute(
            "ALTER TABLE cookies ADD COLUMN last_access_utc "
            "INTEGER DEFAULT 0") ||
        !db_->Execute("UPDATE cookies SET last_access_utc = creation_utc")) {
      LOG(WARNING) << "Unable to update cookie database to version 3.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
  }

  if (cur_version == 3) {
    // The time epoch changed for Mac & Linux in this version to match Windows.
    // This patch came after the main epoch change happened, so some
    // developers have "good" times for cookies added by the more recent
    // versions. So we have to be careful to only update times that are under
    // the old system (which will appear to be from before 1970 in the new
    // system). The magic number used below is 1970 in our time units.
    sql::Transaction transaction(db_.get());
    transaction.Begin();
#if !defined(OS_WIN)
    ignore_result(db_->Execute(
        "UPDATE cookies "
        "SET creation_utc = creation_utc + 11644473600000000 "
        "WHERE rowid IN "
        "(SELECT rowid FROM cookies WHERE "
        "creation_utc > 0 AND creation_utc < 11644473600000000)"));
    ignore_result(db_->Execute(
        "UPDATE cookies "
        "SET expires_utc = expires_utc + 11644473600000000 "
        "WHERE rowid IN "
        "(SELECT rowid FROM cookies WHERE "
        "expires_utc > 0 AND expires_utc < 11644473600000000)"));
    ignore_result(db_->Execute(
        "UPDATE cookies "
        "SET last_access_utc = last_access_utc + 11644473600000000 "
        "WHERE rowid IN "
        "(SELECT rowid FROM cookies WHERE "
        "last_access_utc > 0 AND last_access_utc < 11644473600000000)"));
#endif
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    transaction.Commit();
  }

  if (cur_version == 4) {
    const base::TimeTicks start_time = base::TimeTicks::Now();
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;
    if (!db_->Execute(
            "ALTER TABLE cookies "
            "ADD COLUMN has_expires INTEGER DEFAULT 1") ||
        !db_->Execute(
            "ALTER TABLE cookies "
            "ADD COLUMN persistent INTEGER DEFAULT 1")) {
      LOG(WARNING) << "Unable to update cookie database to version 5.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
    UMA_HISTOGRAM_TIMES("Cookie.TimeDatabaseMigrationToV5",
                        base::TimeTicks::Now() - start_time);
  }

  if (cur_version == 5) {
    const base::TimeTicks start_time = base::TimeTicks::Now();
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;
    // Alter the table to add the priority column with a default value.
    std::string stmt(base::StringPrintf(
        "ALTER TABLE cookies ADD COLUMN priority INTEGER DEFAULT %d",
        CookiePriorityToDBCookiePriority(COOKIE_PRIORITY_DEFAULT)));
    if (!db_->Execute(stmt.c_str())) {
      LOG(WARNING) << "Unable to update cookie database to version 6.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
    UMA_HISTOGRAM_TIMES("Cookie.TimeDatabaseMigrationToV6",
                        base::TimeTicks::Now() - start_time);
  }

  if (cur_version == 6) {
    const base::TimeTicks start_time = base::TimeTicks::Now();
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;
    // Alter the table to add empty "encrypted value" column.
    if (!db_->Execute(
            "ALTER TABLE cookies "
            "ADD COLUMN encrypted_value BLOB DEFAULT ''")) {
      LOG(WARNING) << "Unable to update cookie database to version 7.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
    UMA_HISTOGRAM_TIMES("Cookie.TimeDatabaseMigrationToV7",
                        base::TimeTicks::Now() - start_time);
  }

  if (cur_version == 7) {
    const base::TimeTicks start_time = base::TimeTicks::Now();
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;
    // Alter the table to add a 'firstpartyonly' column.
    if (!db_->Execute(
            "ALTER TABLE cookies "
            "ADD COLUMN firstpartyonly INTEGER DEFAULT 0")) {
      LOG(WARNING) << "Unable to update cookie database to version 8.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
    UMA_HISTOGRAM_TIMES("Cookie.TimeDatabaseMigrationToV8",
                        base::TimeTicks::Now() - start_time);
  }

  if (cur_version == 8) {
    const base::TimeTicks start_time = base::TimeTicks::Now();
    sql::Transaction transaction(db_.get());
    if (!transaction.Begin())
      return false;

    if (!db_->Execute("DROP INDEX IF EXISTS cookie_times")) {
      LOG(WARNING)
          << "Unable to drop table cookie_times in update to version 9.";
      return false;
    }

    if (!db_->Execute(
            "CREATE INDEX IF NOT EXISTS domain ON cookies(host_key)")) {
      LOG(WARNING) << "Unable to create index domain in update to version 9.";
      return false;
    }

#if defined(OS_IOS)
    // iOS 8.1 and older doesn't support partial indices. iOS 8.2 supports
    // partial indices.
    if (!db_->Execute(
            "CREATE INDEX IF NOT EXISTS is_transient ON cookies(persistent)")) {
#else
    if (!db_->Execute(
            "CREATE INDEX IF NOT EXISTS is_transient ON cookies(persistent) "
            "where persistent != 1")) {
#endif
      LOG(WARNING)
          << "Unable to create index is_transient in update to version 9.";
      return false;
    }
    ++cur_version;
    meta_table_.SetVersionNumber(cur_version);
    meta_table_.SetCompatibleVersionNumber(
        std::min(cur_version, kCompatibleVersionNumber));
    transaction.Commit();
    UMA_HISTOGRAM_TIMES("Cookie.TimeDatabaseMigrationToV9",
                        base::TimeTicks::Now() - start_time);
  }

  // Put future migration cases here.

  if (cur_version < kCurrentVersionNumber) {
    UMA_HISTOGRAM_COUNTS_100("Cookie.CorruptMetaTable", 1);

    meta_table_.Reset();
    db_.reset(new sql::Connection);
    if (!sql::Connection::Delete(path_) || !db_->Open(path_) ||
        !meta_table_.Init(db_.get(), kCurrentVersionNumber,
                          kCompatibleVersionNumber)) {
      UMA_HISTOGRAM_COUNTS_100("Cookie.CorruptMetaTableRecoveryFailed", 1);
      NOTREACHED() << "Unable to reset the cookie DB.";
      meta_table_.Reset();
      db_.reset();
      return false;
    }
  }

  return true;
}

void SQLitePersistentCookieStore::Backend::AddCookie(
    const CanonicalCookie& cc) {
  BatchOperation(PendingOperation::COOKIE_ADD, cc);
}

void SQLitePersistentCookieStore::Backend::UpdateCookieAccessTime(
    const CanonicalCookie& cc) {
  BatchOperation(PendingOperation::COOKIE_UPDATEACCESS, cc);
}

void SQLitePersistentCookieStore::Backend::DeleteCookie(
    const CanonicalCookie& cc) {
  BatchOperation(PendingOperation::COOKIE_DELETE, cc);
}

void SQLitePersistentCookieStore::Backend::BatchOperation(
    PendingOperation::OperationType op,
    const CanonicalCookie& cc) {
  // Commit every 30 seconds.
  static const int kCommitIntervalMs = 30 * 1000;
  // Commit right away if we have more than 512 outstanding operations.
  static const size_t kCommitAfterBatchSize = 512;
  DCHECK(!background_task_runner_->RunsTasksInCurrentSequence());

  // We do a full copy of the cookie here, and hopefully just here.
  std::unique_ptr<PendingOperation> po(new PendingOperation(op, cc));

  PendingOperationsList::size_type num_pending;
  {
    base::AutoLock locked(lock_);
    pending_.push_back(po.release());
    num_pending = ++num_pending_;
  }

  if (num_pending == 1) {
    // We've gotten our first entry for this batch, fire off the timer.
    if (!background_task_runner_->PostDelayedTask(
            FROM_HERE, base::Bind(&Backend::Commit, this),
            base::TimeDelta::FromMilliseconds(kCommitIntervalMs))) {
      NOTREACHED() << "background_task_runner_ is not running.";
    }
  } else if (num_pending == kCommitAfterBatchSize) {
    // We've reached a big enough batch, fire off a commit now.
    PostBackgroundTask(FROM_HERE, base::Bind(&Backend::Commit, this));
  }
}

void SQLitePersistentCookieStore::Backend::Commit() {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  {
    base::AutoLock locked(before_flush_callback_lock_);
    if (!before_flush_callback_.is_null())
      before_flush_callback_.Run();
  }

  PendingOperationsList ops;
  {
    base::AutoLock locked(lock_);
    pending_.swap(ops);
    num_pending_ = 0;
  }

  // Maybe an old timer fired or we are already Close()'ed.
  if (!db_.get() || ops.empty())
    return;

  sql::Statement add_smt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO cookies (creation_utc, host_key, name, value, "
      "encrypted_value, path, expires_utc, secure, httponly, firstpartyonly, "
      "last_access_utc, has_expires, persistent, priority) "
      "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
  if (!add_smt.is_valid())
    return;

  sql::Statement update_access_smt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE cookies SET last_access_utc=? WHERE creation_utc=?"));
  if (!update_access_smt.is_valid())
    return;

  sql::Statement del_smt(db_->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM cookies WHERE creation_utc=?"));
  if (!del_smt.is_valid())
    return;

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin())
    return;

  for (PendingOperationsList::iterator it = ops.begin(); it != ops.end();
       ++it) {
    // Free the cookies as we commit them to the database.
    std::unique_ptr<PendingOperation> po(*it);
    switch (po->op()) {
      case PendingOperation::COOKIE_ADD:
        add_smt.Reset(true);
        add_smt.BindInt64(0, po->cc().CreationDate().ToInternalValue());
        add_smt.BindString(1, po->cc().Domain());
        add_smt.BindString(2, po->cc().Name());
        if (crypto_ && crypto_->ShouldEncrypt()) {
          std::string encrypted_value;
          if (!crypto_->EncryptString(po->cc().Value(), &encrypted_value))
            continue;
          add_smt.BindCString(3, "");  // value
          // BindBlob() immediately makes an internal copy of the data.
          add_smt.BindBlob(4, encrypted_value.data(),
                           static_cast<int>(encrypted_value.length()));
        } else {
          add_smt.BindString(3, po->cc().Value());
          add_smt.BindBlob(4, "", 0);  // encrypted_value
        }
        add_smt.BindString(5, po->cc().Path());
        add_smt.BindInt64(6, po->cc().ExpiryDate().ToInternalValue());
        add_smt.BindInt(7, po->cc().IsSecure());
        add_smt.BindInt(8, po->cc().IsHttpOnly());
        add_smt.BindInt(9,
                        CookieSameSiteToDBCookieSameSite(po->cc().SameSite()));
        add_smt.BindInt64(10, po->cc().LastAccessDate().ToInternalValue());
        add_smt.BindInt(11, po->cc().IsPersistent());
        add_smt.BindInt(12, po->cc().IsPersistent());
        add_smt.BindInt(13,
                        CookiePriorityToDBCookiePriority(po->cc().Priority()));
        if (!add_smt.Run())
          NOTREACHED() << "Could not add a cookie to the DB.";
        break;

      case PendingOperation::COOKIE_UPDATEACCESS:
        update_access_smt.Reset(true);
        update_access_smt.BindInt64(
            0, po->cc().LastAccessDate().ToInternalValue());
        update_access_smt.BindInt64(1,
                                    po->cc().CreationDate().ToInternalValue());
        if (!update_access_smt.Run())
          NOTREACHED() << "Could not update cookie last access time in the DB.";
        break;

      case PendingOperation::COOKIE_DELETE:
        del_smt.Reset(true);
        del_smt.BindInt64(0, po->cc().CreationDate().ToInternalValue());
        if (!del_smt.Run())
          NOTREACHED() << "Could not delete a cookie from the DB.";
        break;

      default:
        NOTREACHED();
        break;
    }
  }
  bool succeeded = transaction.Commit();
  UMA_HISTOGRAM_ENUMERATION("Cookie.BackingStoreUpdateResults",
                            succeeded ? 0 : 1, 2);
}

void SQLitePersistentCookieStore::Backend::SetBeforeFlushCallback(
    base::RepeatingClosure callback) {
  base::AutoLock locked(before_flush_callback_lock_);
  before_flush_callback_ = std::move(callback);
}

void SQLitePersistentCookieStore::Backend::Flush(base::OnceClosure callback) {
  DCHECK(!background_task_runner_->RunsTasksInCurrentSequence());
  PostBackgroundTask(FROM_HERE,
                     base::BindOnce(&Backend::FlushAndNotifyInBackground, this,
                                    std::move(callback)));
}

// Fire off a close message to the background runner.  We could still have a
// pending commit timer or Load operations holding references on us, but if/when
// this fires we will already have been cleaned up and it will be ignored.
void SQLitePersistentCookieStore::Backend::Close(
    const base::Closure& callback) {
  if (background_task_runner_->RunsTasksInCurrentSequence()) {
    InternalBackgroundClose(callback);
  } else {
    // Must close the backend on the background runner.
    PostBackgroundTask(FROM_HERE, base::Bind(&Backend::InternalBackgroundClose,
                                             this, callback));
  }
}

void SQLitePersistentCookieStore::Backend::InternalBackgroundClose(
    const base::Closure& callback) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  // Commit any pending operations
  Commit();

  meta_table_.Reset();
  db_.reset();

  // We're clean now.
  if (!callback.is_null())
    callback.Run();
}

void SQLitePersistentCookieStore::Backend::DatabaseErrorCallback(
    int error,
    sql::Statement* stmt) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  if (!sql::IsErrorCatastrophic(error))
    return;

  // TODO(shess): Running KillDatabase() multiple times should be
  // safe.
  if (corruption_detected_)
    return;

  corruption_detected_ = true;

  // Don't just do the close/delete here, as we are being called by |db| and
  // that seems dangerous.
  // TODO(shess): Consider just calling RazeAndClose() immediately.
  // db_ may not be safe to reset at this point, but RazeAndClose()
  // would cause the stack to unwind safely with errors.
  PostBackgroundTask(FROM_HERE, base::Bind(&Backend::KillDatabase, this));
}

void SQLitePersistentCookieStore::Backend::KillDatabase() {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  if (db_) {
    // This Backend will now be in-memory only. In a future run we will recreate
    // the database. Hopefully things go better then!
    bool success = db_->RazeAndClose();
    UMA_HISTOGRAM_BOOLEAN("Cookie.KillDatabaseResult", success);
    meta_table_.Reset();
    db_.reset();
  }
}

void SQLitePersistentCookieStore::Backend::DeleteAllInList(
    const std::list<CookieOrigin>& cookies) {
  if (cookies.empty())
    return;

  if (background_task_runner_->RunsTasksInCurrentSequence()) {
    BackgroundDeleteAllInList(cookies);
  } else {
    // Perform deletion on background task runner.
    PostBackgroundTask(
        FROM_HERE,
        base::Bind(&Backend::BackgroundDeleteAllInList, this, cookies));
  }
}

void SQLitePersistentCookieStore::Backend::DeleteSessionCookiesOnStartup() {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  base::Time start_time = base::Time::Now();
  if (!db_->Execute("DELETE FROM cookies WHERE persistent != 1"))
    LOG(WARNING) << "Unable to delete session cookies.";

  UMA_HISTOGRAM_TIMES("Cookie.Startup.TimeSpentDeletingCookies",
                      base::Time::Now() - start_time);
  UMA_HISTOGRAM_COUNTS_1M("Cookie.Startup.NumberOfCookiesDeleted",
                          db_->GetLastChangeCount());
}

void SQLitePersistentCookieStore::Backend::BackgroundDeleteAllInList(
    const std::list<CookieOrigin>& cookies) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  if (!db_)
    return;

  // Force a commit of any pending writes before issuing deletes.
  // TODO(rohitrao): Remove the need for this Commit() by instead pruning the
  // list of pending operations. https://crbug.com/486742.
  Commit();

  sql::Statement del_smt(db_->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM cookies WHERE host_key=? AND secure=?"));
  if (!del_smt.is_valid()) {
    LOG(WARNING) << "Unable to delete cookies on shutdown.";
    return;
  }

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    LOG(WARNING) << "Unable to delete cookies on shutdown.";
    return;
  }

  for (const auto& cookie : cookies) {
    const GURL url(cookie_util::CookieOriginToURL(cookie.first, cookie.second));
    if (!url.is_valid())
      continue;

    del_smt.Reset(true);
    del_smt.BindString(0, cookie.first);
    del_smt.BindInt(1, cookie.second);
    if (!del_smt.Run())
      NOTREACHED() << "Could not delete a cookie from the DB.";
  }

  if (!transaction.Commit())
    LOG(WARNING) << "Unable to delete cookies on shutdown.";
}

void SQLitePersistentCookieStore::Backend::PostBackgroundTask(
    const base::Location& origin,
    base::OnceClosure task) {
  if (!background_task_runner_->PostTask(origin, std::move(task))) {
    LOG(WARNING) << "Failed to post task from " << origin.ToString()
                 << " to background_task_runner_.";
  }
}

void SQLitePersistentCookieStore::Backend::PostClientTask(
    const base::Location& origin,
    base::OnceClosure task) {
  if (!client_task_runner_->PostTask(origin, std::move(task))) {
    LOG(WARNING) << "Failed to post task from " << origin.ToString()
                 << " to client_task_runner_.";
  }
}

void SQLitePersistentCookieStore::Backend::FinishedLoadingCookies(
    const LoadedCallback& loaded_callback,
    bool success) {
  PostClientTask(FROM_HERE, base::Bind(&Backend::CompleteLoadInForeground, this,
                                       loaded_callback, success));
}

SQLitePersistentCookieStore::SQLitePersistentCookieStore(
    const base::FilePath& path,
    const scoped_refptr<base::SequencedTaskRunner>& client_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& background_task_runner,
    bool restore_old_session_cookies,
    CookieCryptoDelegate* crypto_delegate)
    : backend_(new Backend(path,
                           client_task_runner,
                           background_task_runner,
                           restore_old_session_cookies,
                           crypto_delegate)) {
}

void SQLitePersistentCookieStore::DeleteAllInList(
    const std::list<CookieOrigin>& cookies) {
  if (backend_)
    backend_->DeleteAllInList(cookies);
}

void SQLitePersistentCookieStore::Close(const base::Closure& callback) {
  if (backend_) {
    backend_->Close(callback);

    // We release our reference to the Backend, though it will probably still
    // have a reference if the background runner has not run
    // Backend::InternalBackgroundClose() yet.
    backend_ = nullptr;
  }
}

void SQLitePersistentCookieStore::Load(const LoadedCallback& loaded_callback) {
  DCHECK(!loaded_callback.is_null());
  if (backend_)
    backend_->Load(loaded_callback);
  else
    loaded_callback.Run(std::vector<std::unique_ptr<CanonicalCookie>>());
}

void SQLitePersistentCookieStore::LoadCookiesForKey(
    const std::string& key,
    const LoadedCallback& loaded_callback) {
  DCHECK(!loaded_callback.is_null());
  if (backend_)
    backend_->LoadCookiesForKey(key, loaded_callback);
  else
    loaded_callback.Run(std::vector<std::unique_ptr<CanonicalCookie>>());
}

void SQLitePersistentCookieStore::AddCookie(const CanonicalCookie& cc) {
  if (backend_)
    backend_->AddCookie(cc);
}

void SQLitePersistentCookieStore::UpdateCookieAccessTime(
    const CanonicalCookie& cc) {
  if (backend_)
    backend_->UpdateCookieAccessTime(cc);
}

void SQLitePersistentCookieStore::DeleteCookie(const CanonicalCookie& cc) {
  if (backend_)
    backend_->DeleteCookie(cc);
}

void SQLitePersistentCookieStore::SetForceKeepSessionState() {
  // This store never discards session-only cookies, so this call has no effect.
}

void SQLitePersistentCookieStore::SetBeforeFlushCallback(
    base::RepeatingClosure callback) {
  if (backend_)
    backend_->SetBeforeFlushCallback(std::move(callback));
}

void SQLitePersistentCookieStore::Flush(base::OnceClosure callback) {
  if (backend_)
    backend_->Flush(std::move(callback));
}

SQLitePersistentCookieStore::~SQLitePersistentCookieStore() {
  Close(base::Closure());
}

}  // namespace net
