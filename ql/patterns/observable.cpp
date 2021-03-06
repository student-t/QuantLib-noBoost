/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
Copyright (C) 2013 Chris Higgs
Copyright (C) 2015 Klaus Spanderen

This file is part of QuantLib, a free-software/open-source library
for financial quantitative analysts and developers - http://quantlib.org/

QuantLib is free software: you can redistribute it and/or modify it
under the terms of the QuantLib license.  You should have received a
copy of the license along with this program; if not, please email
<quantlib-dev@lists.sf.net>. The license is also available online at
<http://quantlib.org/license.shtml>.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the license for more details.
*/


#include <ql/patterns/observable.hpp>

#ifndef QL_ENABLE_THREAD_SAFE_OBSERVER_PATTERN

namespace QuantLib {

    void ObservableSettings::enableUpdates() {
        updatesEnabled_  = true;
        updatesDeferred_ = false;

        // if there are outstanding deferred updates, do the notification
        if (deferredObservers_.size()) {
            bool successful = true;
            std::string errMsg;

            for (iterator i=deferredObservers_.begin();
                i!=deferredObservers_.end(); ++i) {
                try {
                    (*i)->update();
                } catch (std::exception& e) {
                    successful = false;
                    errMsg = e.what();
                } catch (...) {
                    successful = false;
                }
            }

            deferredObservers_.clear();

            QL_ENSURE(successful,
                  "could not notify one or more observers: " << errMsg);
        }
    }


    void Observable::notifyObservers() {
        if (!settings_.updatesEnabled()) {
            // if updates are only deferred, flag this for later notification
            // these are held centrally by the settings singleton
            settings_.registerDeferredObservers(observers_);
        }
        else if (observers_.size()) {
            bool successful = true;
            std::string errMsg;
            for (iterator i=observers_.begin(); i!=observers_.end(); ++i) {
                try {
                    (*i)->update();
                } catch (std::exception& e) {
                    // quite a dilemma. If we don't catch the exception,
                    // other observers will not receive the notification
                    // and might be left in an incorrect state. If we do
                    // catch it and continue the loop (as we do here) we
                    // lose the exception. The least evil might be to try
                    // and notify all observers, while raising an
                    // exception if something bad happened.
                    successful = false;
                    errMsg = e.what();
                } catch (...) {
                    successful = false;
                }
            }
            QL_ENSURE(successful,
                  "could not notify one or more observers: " << errMsg);
        }
    }

}

#else

namespace QuantLib {

    void Observable::registerObserver(const std::shared_ptr<Observer::Proxy> &observerProxy) {
        std::scoped_lock<std::recursive_mutex> lock(mutex_);
        observers_.insert(observerProxy);

    }

    void Observable::unregisterObserver(const std::shared_ptr<Observer::Proxy> &observerProxy) {
        {
            std::scoped_lock<std::recursive_mutex> lock(mutex_);
            observers_.erase(observerProxy);
        }

        if (settings_.updatesDeferred()) {
            std::scoped_lock<std::mutex> sLock(settings_.mutex_);
            if (settings_.updatesDeferred()) {
                settings_.unregisterDeferredObserver(observerProxy);
            }
        }
    }

    void Observable::notifyObservers() {
        if (settings_.updatesEnabled()) {
            std::scoped_lock<std::recursive_mutex> lock(mutex_);
            set_type observers_copy_(observers_);
            for (auto const &o : observers_copy_) {
                if (o) {
                    o->update();
                }
            }
            return;
        }

        std::scoped_lock<std::mutex> sLock(settings_.mutex_);
        if (settings_.updatesEnabled()) {
            std::scoped_lock<std::recursive_mutex> lock(mutex_);
            set_type observers_copy_(observers_);
            for (auto const &o : observers_copy_) {
                if (o) {
                    o->update();
                }
            }
            return;
        } else if (settings_.updatesDeferred()) {
            std::scoped_lock<std::recursive_mutex> lock(mutex_);
            // if updates are only deferred, flag this for later notification
            // these are held centrally by the settings singleton
            settings_.registerDeferredObservers(observers_);
        }
    }

    Observable::Observable()
            :observers_(1000), settings_(ObservableSettings::instance()) {}

    Observable::Observable(const Observable &)
            : settings_(ObservableSettings::instance()) {
        // the observer set is not copied; no observer asked to
        // register with this object
    }

}

#endif
