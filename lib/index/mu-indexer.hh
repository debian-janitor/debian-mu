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

#ifndef MU_INDEXER_HH__
#define MU_INDEXER_HH__

#include <memory>
#include <chrono>

namespace Mu {

class Store;

/// An object abstracting the index process.
class Indexer {
public:
         /**
         * Construct an indexer object
         *
         * @param store the message store to use
         */
        Indexer (Store& store);

        /**
         * DTOR
         */
        ~Indexer();

               /// A configuration object for the indexer
        struct Config {
                bool scan{true};
                /**< scan for new messages */
                bool cleanup{true};
                /**< clean messages no longer in the file system */
                size_t max_threads{};
                /**< maximum # of threads to use */
                bool ignore_noupdate{};
                /**< ignore .noupdate files */
                bool lazy_check{};
                /**< whether to skip directories that don't have a changed
                 * mtime */
        };


        /**
         * Start indexing. If already underway, do nothing.
         *
         * @param conf a configuration object
         *
         * @return true if starting worked or an indexing process was already
         * underway; false otherwise.
         *
         */
        bool start(const Config& conf);

        /**
         * Stop indexing. If not indexing, do nothing.
         *
         *
         * @return true if we stopped indexing, or indexing was not underway.
         * False otherwise.
         */
        bool stop();

        /**
         * Is an indexing process running?
         *
         * @return true or false.
         */
        bool is_running() const;


        // Object describing current progress
        struct Progress {
                bool   running{};   /**< Is an index operation in progress? */
                size_t processed{}; /**< Number of messages processed */
                size_t updated{};   /**< Number of messages added/updated to store */
                size_t removed{};   /**< Number of message removed from store */
        };

        /**
         * Get an object describing the current progress. The progress object
         * describes the most recent indexing job, and is reset up a fresh
         * start().
         *
         * @return a progress object.
         */
        Progress progress() const;

private:
        struct                   Private;
        std::unique_ptr<Private> priv_;
};



} // namespace Mu
#endif /* MU_INDEXER_HH__ */
