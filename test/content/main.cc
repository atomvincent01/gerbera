/*GRB*

    Gerbera - https://gerbera.io/

    test/content/main.cc - this file is part of Gerbera.

    Copyright (C) 2016-2026 Gerbera Contributors

    Gerbera is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    Gerbera is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Gerbera.  If not, see <http://www.gnu.org/licenses/>.

    $Id$
*/

#include "util/logger.h"

#include <gmock/gmock.h>

int main(int argc, char** argv)
{
    testing::InitGoogleMock(&argc, argv);
#ifdef GRBDEBUG
    GrbLogger::Logger.init(GrbLogger::makeFacility("Thread|Sqlite3|SQL|Content|Autoscan|Inotify|Script|Layout"));
#endif
    spdlog::set_pattern("%Y-%m-%dT%H:%M:%S.%e [%P:%t] %l [%s:%# %!] %v");
    spdlog::set_level(spdlog::level::debug);
    int ret = RUN_ALL_TESTS();
    return ret;
}
