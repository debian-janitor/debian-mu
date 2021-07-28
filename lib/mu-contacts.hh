/*
** Copyright (C) 2020 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/

#ifndef __MU_CONTACTS_HH__
#define __MU_CONTACTS_HH__

#include <glib.h>
#include <time.h>
#include <memory>
#include <functional>
#include <chrono>
#include <string>
#include <time.h>
#include <inttypes.h>
#include <utils/mu-utils.hh>

namespace Mu {

/// Data-structure representing information about some contact.

struct ContactInfo {
        /**
         * Construct a new ContactInfo
         *
         * @param _full_address the full email address + name.
         * @param _email email address
         * @param _name name or empty
         * @param _personal is this a personal contact?
         * @param _last_seen when was this contact last seen?
         * @param _freq how often was this contact seen?
         */
        ContactInfo (const std::string& _full_address,
                     const std::string& _email,
                     const std::string& _name,
                     bool personal,
                     time_t _last_seen,
                     size_t freq=1);

        std::string full_address; /**< Full name <email> */
        std::string email;        /**< email address */
        std::string name;         /**< name (or empty) */
        bool        personal{};   /**< is this a personal contact? */
        time_t      last_seen{};  /**< when was this contact last seen? */
        std::size_t freq{};       /**< how often was this contact seen? */

        int64_t     tstamp{};     /**< Time-stamp, as per g_get_monotonic_time */
};

/// All contacts
class Contacts {
public:
        /**
         * Construct a new contacts objects
         *
         * @param serialized serialized contacts
         * @param personal personal addresses
         */
        Contacts (const std::string& serialized = "",
                  const StringVec& personal={});

        /**
         * DTOR
         *
         */
        ~Contacts ();

        /**
         * Add a contact
         *
         * @param ci A contact-info object
         *
         * @return the inserted / updated / washed contact info. Note that
         * this is return _as copy_ to make it thread-safe.
         */
        const ContactInfo add(ContactInfo&& ci);

        /**
         * Clear all contacts
         *
         */
        void clear();

        /**
         * Get the number of contacts
         *

         * @return number of contacts
         */
        std::size_t size() const;

        /**
         * Are there no contacts?
         *
         * @return true or false
         */
        bool empty() const { return size() == 0; }

        /**
         * Get the contacts, serialized.
         *
         * @return serialized contacts
         */
        std::string serialize() const;


        /**
         * Does this look like a 'personal' address?
         *
         * @param addr some e-mail address
         *
         * @return true or false
         */
        bool is_personal(const std::string& addr) const;

        /**
         * Find a contact based on the email address. This is not safe, since
         * the returned ptr can be invalidated at any time; only for unit-tests.
         *
         * @param email email address
         *
         * @return contact info, or {} if not found
         */
        const ContactInfo* _find (const std::string& email) const;

        /**
         * Prototype for a callable that receives a contact
         *
         * @param contact some contact
         */
        using EachContactFunc = std::function<void (const ContactInfo& contact_info)>;

        /**
         * Invoke some callable for each contact, in order of rank.
         *
         * @param each_contact
         */
        void for_each (const EachContactFunc& each_contact) const;

private:
        struct                   Private;
        std::unique_ptr<Private> priv_;
};

} // namespace Mu

#endif /* __MU_CONTACTS_HH__ */
