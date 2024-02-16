#include "quiche_platform_impl/quiche_mutex_impl.h"

namespace quiche {

void QuicheLockImpl::WriterLock() { mu_.WriterLock(); }

void QuicheLockImpl::WriterUnlock() { mu_.WriterUnlock(); }

void QuicheLockImpl::ReaderLock() { mu_.ReaderLock(); }

void QuicheLockImpl::ReaderUnlock() { mu_.ReaderUnlock(); }

void QuicheLockImpl::AssertReaderHeld() const { mu_.AssertReaderHeld(); }

}  // namespace quiche
