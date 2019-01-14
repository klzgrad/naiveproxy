// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/extras/sqlite/sqlite_channel_id_store.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "crypto/ec_private_key.h"
#include "net/ssl/channel_id_service.h"
#include "net/ssl/ssl_client_cert_type.h"
#include "net/test/cert_test_util.h"
#include "net/test/channel_id_test_util.h"
#include "net/test/test_data_directory.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

const base::FilePath::CharType kTestChannelIDFilename[] =
    FILE_PATH_LITERAL("ChannelID");

class SQLiteChannelIDStoreTest : public TestWithScopedTaskEnvironment {
 public:
  void Load(std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>>*
                channel_ids) {
    base::RunLoop run_loop;
    store_->Load(base::Bind(&SQLiteChannelIDStoreTest::OnLoaded,
                            base::Unretained(this),
                            &run_loop));
    run_loop.Run();
    channel_ids->swap(channel_ids_);
    channel_ids_.clear();
  }

  void OnLoaded(
      base::RunLoop* run_loop,
      std::unique_ptr<std::vector<
          std::unique_ptr<DefaultChannelIDStore::ChannelID>>> channel_ids) {
    channel_ids_.swap(*channel_ids);
    run_loop->Quit();
  }

 protected:
  static void ReadLegacyTestKeyAndCert(std::string* key_data,
                                       std::string* cert_data) {
    base::FilePath key_path =
        GetTestCertsDirectory().AppendASCII("unittest.originbound.key.der");
    base::FilePath cert_path =
        GetTestCertsDirectory().AppendASCII("unittest.originbound.der");
    ASSERT_TRUE(base::ReadFileToString(key_path, key_data));
    ASSERT_TRUE(base::ReadFileToString(cert_path, cert_data));
  }

  static base::Time GetTestCertExpirationTime() {
    // Cert expiration time from 'openssl asn1parse -inform der -in
    // unittest.originbound.der':
    // UTCTIME           :160507022239Z
    // base::Time::FromUTCExploded can't generate values past 2038 on 32-bit
    // linux, so we use the raw value here.
    base::Time::Exploded exploded_time;
    exploded_time.year = 2016;
    exploded_time.month = 5;
    exploded_time.day_of_week = 0;  // Unused.
    exploded_time.day_of_month = 7;
    exploded_time.hour = 2;
    exploded_time.minute = 22;
    exploded_time.second = 39;
    exploded_time.millisecond = 0;
    base::Time out_time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded_time, &out_time));
    return out_time;
  }

  static base::Time GetTestCertCreationTime() {
    // UTCTIME           :150508022239Z
    base::Time::Exploded exploded_time;
    exploded_time.year = 2015;
    exploded_time.month = 5;
    exploded_time.day_of_week = 0;  // Unused.
    exploded_time.day_of_month = 8;
    exploded_time.hour = 2;
    exploded_time.minute = 22;
    exploded_time.second = 39;
    exploded_time.millisecond = 0;
    base::Time out_time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded_time, &out_time));
    return out_time;
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    store_ = new SQLiteChannelIDStore(
        temp_dir_.GetPath().Append(kTestChannelIDFilename),
        base::ThreadTaskRunnerHandle::Get());
    std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
    Load(&channel_ids);
    ASSERT_EQ(0u, channel_ids.size());
    // Make sure the store gets written at least once.
    google_key_ = crypto::ECPrivateKey::Create();
    store_->AddChannelID(DefaultChannelIDStore::ChannelID(
        "google.com", base::Time::FromInternalValue(1), google_key_->Copy()));
  }

  base::ScopedTempDir temp_dir_;
  scoped_refptr<SQLiteChannelIDStore> store_;
  std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids_;
  std::unique_ptr<crypto::ECPrivateKey> google_key_;
};

// Test if data is stored as expected in the SQLite database.
TEST_F(SQLiteChannelIDStoreTest, TestPersistence) {
  std::unique_ptr<crypto::ECPrivateKey> foo_key(crypto::ECPrivateKey::Create());
  store_->AddChannelID(DefaultChannelIDStore::ChannelID(
      "foo.com", base::Time::FromInternalValue(3), foo_key->Copy()));

  std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
  // Replace the store effectively destroying the current one and forcing it
  // to write its data to disk. Then we can see if after loading it again it
  // is still there.
  store_ = NULL;
  // Make sure we wait until the destructor has run.
  base::RunLoop().RunUntilIdle();
  store_ = new SQLiteChannelIDStore(
      temp_dir_.GetPath().Append(kTestChannelIDFilename),
      base::ThreadTaskRunnerHandle::Get());

  // Reload and test for persistence
  Load(&channel_ids);
  ASSERT_EQ(2U, channel_ids.size());
  DefaultChannelIDStore::ChannelID* goog_channel_id;
  DefaultChannelIDStore::ChannelID* foo_channel_id;
  if (channel_ids[0]->server_identifier() == "google.com") {
    goog_channel_id = channel_ids[0].get();
    foo_channel_id = channel_ids[1].get();
  } else {
    goog_channel_id = channel_ids[1].get();
    foo_channel_id = channel_ids[0].get();
  }
  ASSERT_EQ("google.com", goog_channel_id->server_identifier());
  EXPECT_TRUE(KeysEqual(google_key_.get(), goog_channel_id->key()));
  ASSERT_EQ(1, goog_channel_id->creation_time().ToInternalValue());
  ASSERT_EQ("foo.com", foo_channel_id->server_identifier());
  EXPECT_TRUE(KeysEqual(foo_key.get(), foo_channel_id->key()));
  ASSERT_EQ(3, foo_channel_id->creation_time().ToInternalValue());

  // Now delete the keypair and check persistence again.
  store_->DeleteChannelID(*channel_ids[0]);
  store_->DeleteChannelID(*channel_ids[1]);
  store_ = NULL;
  // Make sure we wait until the destructor has run.
  base::RunLoop().RunUntilIdle();
  channel_ids.clear();
  store_ = new SQLiteChannelIDStore(
      temp_dir_.GetPath().Append(kTestChannelIDFilename),
      base::ThreadTaskRunnerHandle::Get());

  // Reload and check if the keypair has been removed.
  Load(&channel_ids);
  ASSERT_EQ(0U, channel_ids.size());
  // Close the store.
  store_ = NULL;
  // Make sure we wait until the destructor has run.
  base::RunLoop().RunUntilIdle();
}

// Test if data is stored as expected in the SQLite database.
TEST_F(SQLiteChannelIDStoreTest, TestDeleteAll) {
  store_->AddChannelID(DefaultChannelIDStore::ChannelID(
      "foo.com", base::Time::FromInternalValue(3),
      crypto::ECPrivateKey::Create()));

  std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
  // Replace the store effectively destroying the current one and forcing it
  // to write its data to disk. Then we can see if after loading it again it
  // is still there.
  store_ = NULL;
  // Make sure we wait until the destructor has run.
  base::RunLoop().RunUntilIdle();
  store_ = new SQLiteChannelIDStore(
      temp_dir_.GetPath().Append(kTestChannelIDFilename),
      base::ThreadTaskRunnerHandle::Get());

  // Reload and test for persistence
  Load(&channel_ids);
  ASSERT_EQ(2U, channel_ids.size());
  // DeleteAll except foo.com (shouldn't fail if one is missing either).
  std::list<std::string> delete_server_identifiers;
  delete_server_identifiers.push_back("google.com");
  delete_server_identifiers.push_back("missing.com");
  store_->DeleteAllInList(delete_server_identifiers);

  // Now check persistence again.
  store_ = NULL;
  // Make sure we wait until the destructor has run.
  base::RunLoop().RunUntilIdle();
  channel_ids.clear();
  store_ = new SQLiteChannelIDStore(
      temp_dir_.GetPath().Append(kTestChannelIDFilename),
      base::ThreadTaskRunnerHandle::Get());

  // Reload and check that only foo.com persisted in store.
  Load(&channel_ids);
  ASSERT_EQ(1U, channel_ids.size());
  ASSERT_EQ("foo.com", channel_ids[0]->server_identifier());
  // Close the store.
  store_ = NULL;
  // Make sure we wait until the destructor has run.
  base::RunLoop().RunUntilIdle();
}

TEST_F(SQLiteChannelIDStoreTest, TestUpgradeV1) {
  // Reset the store.  We'll be using a different database for this test.
  store_ = NULL;

  base::FilePath v1_db_path(temp_dir_.GetPath().AppendASCII("v1db"));

  std::string key_data;
  std::string cert_data;
  ASSERT_NO_FATAL_FAILURE(ReadLegacyTestKeyAndCert(&key_data, &cert_data));

  // Create a version 1 database.
  {
    sql::Database db;
    ASSERT_TRUE(db.Open(v1_db_path));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY,"
        "value LONGVARCHAR);"
        "INSERT INTO \"meta\" VALUES('version','1');"
        "INSERT INTO \"meta\" VALUES('last_compatible_version','1');"
        "CREATE TABLE origin_bound_certs ("
        "origin TEXT NOT NULL UNIQUE PRIMARY KEY,"
        "private_key BLOB NOT NULL,cert BLOB NOT NULL);"));

    sql::Statement add_smt(db.GetUniqueStatement(
        "INSERT INTO origin_bound_certs (origin, private_key, cert) "
        "VALUES (?,?,?)"));
    add_smt.BindString(0, "google.com");
    add_smt.BindBlob(1, key_data.data(), key_data.size());
    add_smt.BindBlob(2, cert_data.data(), cert_data.size());
    ASSERT_TRUE(add_smt.Run());

    ASSERT_TRUE(db.Execute(
        "INSERT INTO \"origin_bound_certs\" VALUES("
        "'foo.com',X'AA',X'BB');"));
  }

  // Load and test the DB contents twice. The first time checks that the
  // migration ran properly; the second time ensures that the DB is still
  // readable post-migration.
  //
  // Since the V1 format is unsupported, the second load will be reading an
  // empty database.
  for (int i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);

    std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
    store_ = new SQLiteChannelIDStore(v1_db_path,
                                      base::ThreadTaskRunnerHandle::Get());

    // Load the database. Because the existing v1 certs are implicitly of type
    // RSA, which is unsupported, they're discarded.
    Load(&channel_ids);
    ASSERT_EQ(0U, channel_ids.size());

    store_ = NULL;
    base::RunLoop().RunUntilIdle();

    // Verify the database version is updated.
    {
      sql::Database db;
      ASSERT_TRUE(db.Open(v1_db_path));
      sql::Statement smt(db.GetUniqueStatement(
          "SELECT value FROM meta WHERE key = \"version\""));
      ASSERT_TRUE(smt.Step());
      EXPECT_EQ(6, smt.ColumnInt(0));
      EXPECT_FALSE(smt.Step());
    }
  }
}

TEST_F(SQLiteChannelIDStoreTest, TestUpgradeV2) {
  // Reset the store.  We'll be using a different database for this test.
  store_ = NULL;

  base::FilePath v2_db_path(temp_dir_.GetPath().AppendASCII("v2db"));

  std::string key_data;
  std::string cert_data;
  ASSERT_NO_FATAL_FAILURE(ReadLegacyTestKeyAndCert(&key_data, &cert_data));

  // Create a version 2 database.
  {
    sql::Database db;
    ASSERT_TRUE(db.Open(v2_db_path));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY,"
        "value LONGVARCHAR);"
        "INSERT INTO \"meta\" VALUES('version','2');"
        "INSERT INTO \"meta\" VALUES('last_compatible_version','1');"
        "CREATE TABLE origin_bound_certs ("
        "origin TEXT NOT NULL UNIQUE PRIMARY KEY,"
        "private_key BLOB NOT NULL,"
        "cert BLOB NOT NULL,"
        "cert_type INTEGER);"));

    sql::Statement add_smt(db.GetUniqueStatement(
        "INSERT INTO origin_bound_certs (origin, private_key, cert, cert_type) "
        "VALUES (?,?,?,?)"));
    add_smt.BindString(0, "google.com");
    add_smt.BindBlob(1, key_data.data(), key_data.size());
    add_smt.BindBlob(2, cert_data.data(), cert_data.size());
    add_smt.BindInt64(3, 64);
    ASSERT_TRUE(add_smt.Run());

    // Malformed certs will be ignored and not migrated.
    ASSERT_TRUE(db.Execute(
        "INSERT INTO \"origin_bound_certs\" VALUES("
        "'foo.com',X'AA',X'BB',64);"));
  }

  // Load and test the DB contents twice. The first time checks that the
  // migration ran properly; the second time ensures that the DB is still
  // readable post-migration.
  //
  // Since the V2 format is unsupported, the second load will be reading an
  // empty database.
  for (int i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);

    std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
    store_ = new SQLiteChannelIDStore(v2_db_path,
                                      base::ThreadTaskRunnerHandle::Get());

    // Load the database. V2 cert keys are stored in a format that is
    // unsupported, so they will be discarded.
    Load(&channel_ids);
    ASSERT_EQ(0U, channel_ids.size());

    store_ = NULL;
    // Make sure we wait until the destructor has run.
    base::RunLoop().RunUntilIdle();

    // Verify the database version is updated.
    {
      sql::Database db;
      ASSERT_TRUE(db.Open(v2_db_path));
      sql::Statement smt(db.GetUniqueStatement(
          "SELECT value FROM meta WHERE key = \"version\""));
      ASSERT_TRUE(smt.Step());
      EXPECT_EQ(6, smt.ColumnInt(0));
      EXPECT_FALSE(smt.Step());
    }
  }
}

TEST_F(SQLiteChannelIDStoreTest, TestUpgradeV3) {
  // Reset the store.  We'll be using a different database for this test.
  store_ = NULL;

  base::FilePath v3_db_path(temp_dir_.GetPath().AppendASCII("v3db"));

  std::string key_data;
  std::string cert_data;
  ASSERT_NO_FATAL_FAILURE(ReadLegacyTestKeyAndCert(&key_data, &cert_data));

  // Create a version 3 database.
  {
    sql::Database db;
    ASSERT_TRUE(db.Open(v3_db_path));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY,"
        "value LONGVARCHAR);"
        "INSERT INTO \"meta\" VALUES('version','3');"
        "INSERT INTO \"meta\" VALUES('last_compatible_version','1');"
        "CREATE TABLE origin_bound_certs ("
        "origin TEXT NOT NULL UNIQUE PRIMARY KEY,"
        "private_key BLOB NOT NULL,"
        "cert BLOB NOT NULL,"
        "cert_type INTEGER,"
        "expiration_time INTEGER);"));

    sql::Statement add_smt(db.GetUniqueStatement(
        "INSERT INTO origin_bound_certs (origin, private_key, cert, cert_type, "
        "expiration_time) VALUES (?,?,?,?,?)"));
    add_smt.BindString(0, "google.com");
    add_smt.BindBlob(1, key_data.data(), key_data.size());
    add_smt.BindBlob(2, cert_data.data(), cert_data.size());
    add_smt.BindInt64(3, 64);
    add_smt.BindInt64(4, 1000);
    ASSERT_TRUE(add_smt.Run());

    // Malformed certs will be ignored and not migrated.
    ASSERT_TRUE(db.Execute(
        "INSERT INTO \"origin_bound_certs\" VALUES("
        "'foo.com',X'AA',X'BB',64,2000);"));
  }

  // Load and test the DB contents twice. The first time checks that the
  // migration ran properly; the second time ensures that the DB is still
  // readable post-migration.
  //
  // Since the V3 format is unsupported, the second load will be reading an
  // empty database.
  for (int i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);

    std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
    store_ = new SQLiteChannelIDStore(v3_db_path,
                                      base::ThreadTaskRunnerHandle::Get());

    // Load the database. V3 cert keys are in a format that is no longer
    // supported, so they are discarded.
    Load(&channel_ids);
    ASSERT_EQ(0U, channel_ids.size());

    store_ = NULL;
    // Make sure we wait until the destructor has run.
    base::RunLoop().RunUntilIdle();

    // Verify the database version is updated.
    {
      sql::Database db;
      ASSERT_TRUE(db.Open(v3_db_path));
      sql::Statement smt(db.GetUniqueStatement(
          "SELECT value FROM meta WHERE key = \"version\""));
      ASSERT_TRUE(smt.Step());
      EXPECT_EQ(6, smt.ColumnInt(0));
      EXPECT_FALSE(smt.Step());
    }
  }
}

TEST_F(SQLiteChannelIDStoreTest, TestUpgradeV4) {
  // Reset the store.  We'll be using a different database for this test.
  store_ = NULL;

  base::FilePath v4_db_path(temp_dir_.GetPath().AppendASCII("v4db"));

  std::string key_data;
  std::string cert_data;
  ASSERT_NO_FATAL_FAILURE(ReadLegacyTestKeyAndCert(&key_data, &cert_data));

  // Create a version 4 database.
  {
    sql::Database db;
    ASSERT_TRUE(db.Open(v4_db_path));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY,"
        "value LONGVARCHAR);"
        "INSERT INTO \"meta\" VALUES('version','4');"
        "INSERT INTO \"meta\" VALUES('last_compatible_version','1');"
        "CREATE TABLE origin_bound_certs ("
        "origin TEXT NOT NULL UNIQUE PRIMARY KEY,"
        "private_key BLOB NOT NULL,"
        "cert BLOB NOT NULL,"
        "cert_type INTEGER,"
        "expiration_time INTEGER,"
        "creation_time INTEGER);"));

    sql::Statement add_smt(db.GetUniqueStatement(
        "INSERT INTO origin_bound_certs (origin, private_key, cert, cert_type, "
        "expiration_time, creation_time) VALUES (?,?,?,?,?,?)"));
    add_smt.BindString(0, "google.com");
    add_smt.BindBlob(1, key_data.data(), key_data.size());
    add_smt.BindBlob(2, cert_data.data(), cert_data.size());
    add_smt.BindInt64(3, 64);
    add_smt.BindInt64(4, 1000);
    add_smt.BindInt64(5, GetTestCertCreationTime().ToInternalValue());
    ASSERT_TRUE(add_smt.Run());

    // Add an RSA cert to the db. This cert should be ignored in the migration.
    add_smt.Clear();
    add_smt.Assign(db.GetUniqueStatement(
        "INSERT INTO origin_bound_certs "
        "(origin, private_key, cert, cert_type, expiration_time, creation_time)"
        " VALUES (?,?,?,?,?,?)"));
    add_smt.BindString(0, "foo.com");
    add_smt.BindBlob(1, key_data.data(), key_data.size());
    add_smt.BindBlob(2, cert_data.data(), cert_data.size());
    add_smt.BindInt64(3, 1);
    add_smt.BindInt64(4, GetTestCertExpirationTime().ToInternalValue());
    add_smt.BindInt64(5, base::Time::Now().ToInternalValue());
    ASSERT_TRUE(add_smt.Run());

    // Malformed certs will be ignored and not migrated.
    ASSERT_TRUE(db.Execute(
        "INSERT INTO \"origin_bound_certs\" VALUES("
        "'bar.com',X'AA',X'BB',64,2000,3000);"));
  }

  // Load and test the DB contents twice. The first time checks that the
  // migration ran properly; the second time ensures that the DB is still
  // readable post-migration.
  //
  // Since the V4 format is unsupported, the second load will be reading an
  // empty database.
  for (int i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);

    std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
    store_ = new SQLiteChannelIDStore(v4_db_path,
                                      base::ThreadTaskRunnerHandle::Get());

    // Load the database. V4 cert keys are in a format that is no longer
    // supported, so they are discarded.
    Load(&channel_ids);
    ASSERT_EQ(0U, channel_ids.size());

    store_ = NULL;
    // Make sure we wait until the destructor has run.
    base::RunLoop().RunUntilIdle();

    // Verify the database version is updated.
    {
      sql::Database db;
      ASSERT_TRUE(db.Open(v4_db_path));
      sql::Statement smt(db.GetUniqueStatement(
          "SELECT value FROM meta WHERE key = \"version\""));
      ASSERT_TRUE(smt.Step());
      EXPECT_EQ(6, smt.ColumnInt(0));
      EXPECT_FALSE(smt.Step());
    }
  }
}

TEST_F(SQLiteChannelIDStoreTest, TestUpgradeV5) {
  // Reset the store.  We'll be using a different database for this test.
  store_ = NULL;

  base::FilePath v5_db_path(temp_dir_.GetPath().AppendASCII("v5db"));

  std::string key_data;
  std::string cert_data;
  ASSERT_NO_FATAL_FAILURE(ReadLegacyTestKeyAndCert(&key_data, &cert_data));

  // Create a version 5 database.
  {
    sql::Database db;
    ASSERT_TRUE(db.Open(v5_db_path));
    ASSERT_TRUE(db.Execute(
        "CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY,"
        "value LONGVARCHAR);"
        "INSERT INTO \"meta\" VALUES('version','5');"
        "INSERT INTO \"meta\" VALUES('last_compatible_version','5');"
        "CREATE TABLE channel_id ("
        "host TEXT NOT NULL UNIQUE PRIMARY KEY,"
        "private_key BLOB NOT NULL,"
        "public_key BLOB NOT NULL,"
        "creation_time INTEGER);"));

    sql::Statement add_smt(db.GetUniqueStatement(
        "INSERT INTO channel_id (host, private_key, public_key, creation_time) "
        "VALUES (?,?,?,?)"));
    add_smt.BindString(0, "google.com");
    add_smt.BindBlob(1, key_data.data(), key_data.size());
    add_smt.BindBlob(2, "", 0);
    add_smt.BindInt64(3, GetTestCertCreationTime().ToInternalValue());
    ASSERT_TRUE(add_smt.Run());

    // Malformed keys will be ignored and not migrated.
    ASSERT_TRUE(
        db.Execute("INSERT INTO \"channel_id\" VALUES("
                   "'bar.com',X'AA',X'BB',3000);"));
  }

  // Load and test the DB contents twice. The first time checks that the
  // migration ran properly; the second time ensures that the DB is still
  // readable post-migration.
  //
  // Since the V5 format is unsupported, the second load will be reading an
  // empty database.
  for (int i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);

    std::vector<std::unique_ptr<DefaultChannelIDStore::ChannelID>> channel_ids;
    store_ = new SQLiteChannelIDStore(v5_db_path,
                                      base::ThreadTaskRunnerHandle::Get());

    // Load the database. V5 private keys are in a format that is no longer
    // supported, so the keys get deleted in the migration.
    Load(&channel_ids);
    ASSERT_EQ(0U, channel_ids.size());

    store_ = NULL;
    // Make sure we wait until the destructor has run.
    base::RunLoop().RunUntilIdle();

    // Verify the database version is updated.
    {
      sql::Database db;
      ASSERT_TRUE(db.Open(v5_db_path));
      sql::Statement smt(db.GetUniqueStatement(
          "SELECT value FROM meta WHERE key = \"version\""));
      ASSERT_TRUE(smt.Step());
      EXPECT_EQ(6, smt.ColumnInt(0));
      EXPECT_FALSE(smt.Step());
    }
  }
}

}  // namespace net
