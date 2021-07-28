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
#include <config.h>

#include <vector>
#include <glib.h>

#include <iostream>
#include <sstream>
#include <unistd.h>

#include "mu-store.hh"
#include "mu-query.hh"
#include "index/mu-indexer.hh"
#include "utils/mu-utils.hh"
#include "test-mu-common.hh"

using namespace Mu;

static void
test_query()
{
        allow_warnings();

	Store store{test_mu_common_get_random_tmpdir(), std::string{MU_TESTMAILDIR}, {},{}};
        auto&& idx{store.indexer()};

	g_assert_true (idx.start(Indexer::Config{}));
        while (idx.is_running()) {
                sleep(1);
        }

        auto dump_matches=[](const QueryResults& res) {
                size_t n{};
                for (auto&& item: res)
                        g_debug ("%02zu %s %s", ++n, item.path().value_or("<none>").c_str(),
                                 item.message_id().value_or("<none>").c_str());
        };


        Query q{store};
        g_assert_cmpuint(store.size(),==,19);

        {
                const auto res = q.run("", MU_MSG_FIELD_ID_NONE, QueryFlags::None);
                g_assert_true(!!res);
                g_assert_cmpuint(res->size(),==,19);
                dump_matches(*res);
        }

        {
                const auto res = q.run("", MU_MSG_FIELD_ID_PATH, QueryFlags::None, 11);
                g_assert_true(!!res);
                g_assert_cmpuint(res->size(),==,11);
                dump_matches(*res);
        }
}

int
main (int argc, char *argv[]) try
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/query", test_query);

	return g_test_run ();


} catch (const std::runtime_error& re) {
	std::cerr << re.what() << "\n";
	return 1;
} catch (...) {
        std::cerr << "caught exception\n";
        return 1;
}
