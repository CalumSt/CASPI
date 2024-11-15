#ifndef CASPI_SCOPEDLOCK_H
#define CASPI_SCOPEDLOCK_H
namespace CASPI {
/*
 * ScopedLock is a helper class that locks a mutex when it's created and
 * unlocks it when it goes out of scope.
 * Provided as a utility to make it easier to write thread-safe code.
 * Example use:
 * At beginning of a function:
 *     ScopedLock lock(mutex_);
 */
class ScopedLock(std::mutex& _mutex) :mutex(_mutex) {
        mutex.lock();

    ~ScopedLock() {
        mutex.unlock();
    }

    private:
        std::mutex& mutex;
}
}


#endif //CASPI_SCOPEDLOCK_H
