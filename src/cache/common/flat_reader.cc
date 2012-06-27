// vim:set noet cinoptions= sw=4 ts=4:
// This file is part of the eix project and distributed under the
// terms of the GNU General Public License v2.
//
// Copyright (c)
//   Wolfgang Frisch <xororand@users.sourceforge.net>
//   Emil Beinroth <emilbeinroth@gmx.net>
//   Martin Väth <vaeth@mathematik.uni-wuerzburg.de>

#include "flat_reader.h"
#include <cache/base.h>
#include <eixTk/formated.h>
#include <eixTk/i18n.h>
#include <eixTk/likely.h>
#include <eixTk/null.h>
#include <portage/depend.h>
#include <portage/package.h>

#include <fstream>
#include <limits>
#include <string>

#include <cerrno>
#include <cstring>

using namespace std;

inline bool
skip_lines(const int nr, ifstream &is, const string &filename, BasicCache::ErrorCallback error_callback)
{
	for(int i(nr); likely(i > 0); --i) {
		is.ignore(numeric_limits<int>::max(), '\n');
		if(is.fail())
		{
			error_callback(eix::format(_("Can't read cache file %s: %s"))
				% filename % strerror(errno));
			return false;
		}
	}
	return true;
}

/** Read the keywords and slot from a flat cache file. */
void
flat_get_keywords_slot_iuse_restrict(const string &filename, string &keywords, string &slotname, string &iuse, string &restr, string &props, Depend &dep, BasicCache::ErrorCallback error_callback)
{
	ifstream is(filename.c_str());
	if(!is.is_open()) {
		error_callback(eix::format(_("Can't open %s: %s"))
			% filename % strerror(errno));
	}
	string depend, rdepend, pdepend;
	bool use_dep(Depend::use_depend);
	if(use_dep) {
		getline(is, depend);
		getline(is, rdepend);
	}
	else {
		skip_lines(2, is, filename, error_callback);
	}
	getline(is, slotname);
	skip_lines(1, is, filename, error_callback);
	getline(is, restr);
	skip_lines(3, is, filename, error_callback);
	getline(is, keywords);
	skip_lines(1, is, filename, error_callback);
	getline(is, iuse);
	if(use_dep) {
		skip_lines(1, is, filename, error_callback);
		getline(is, pdepend);
		dep.set(depend, rdepend, pdepend, false);
		skip_lines(2, is, filename, error_callback);
	}
	else {
		skip_lines(4, is, filename, error_callback);
	}
	getline(is, props);
	is.close();
}

/** Read a flat cache file. */
void
flat_read_file(const char *filename, Package *pkg, BasicCache::ErrorCallback error_callback)
{
	ifstream is(filename);
	if(!is.is_open()) {
		error_callback(eix::format(_("Can't open %s: %s"))
			% filename % strerror(errno));
	}
	skip_lines(5, is, filename, error_callback);
	string linebuf;
	// Read the rest
	for(int linenr(5); likely(getline(is, linebuf) != NULLPTR); ++linenr) {
		switch(linenr)
		{
			case 5:  pkg->homepage = linebuf;
			         break;
			case 6:  pkg->licenses = linebuf;
			         break;
			case 7:  pkg->desc     = linebuf;
			         is.close(); return;
			default:
				break;
		}
	}
	is.close();
}
