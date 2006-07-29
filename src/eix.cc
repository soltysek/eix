/***************************************************************************
 *   eix is a small utility for searching ebuilds in the                   *
 *   Gentoo Linux portage system. It uses indexing to allow quick searches *
 *   in package descriptions with regular expressions.                     *
 *                                                                         *
 *   https://sourceforge.net/projects/eix                                  *
 *                                                                         *
 *   Copyright (c)                                                         *
 *     Wolfgang Frisch <xororand@users.sourceforge.net>                    *
 *     Emil Beinroth <emilbeinroth@gmx.net>                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "../config.h"

#include <global.h>

#include <output/formatstring.h>
#include <output/formatstring-print.h>

#include <portage/conf/portagesettings.h>

#include <eixTk/argsreader.h>
#include <eixTk/stringutils.h>
#include <eixTk/ansicolor.h>
#include <eixTk/utils.h>

#include <cli.h>

#include <database/header.h>
#include <database/package_reader.h>

#include <signal.h> /* signal handlers */
#include <vector>
#include <stack>

#include <stdio.h>
#include <unistd.h>

using namespace std;

/** The name under which we have been called. */
const char *program_name = NULL;

int  is_current_dbversion(const char *filename);
void print_unused(const char *filename, const eix::ptr_list<Package> &packagelist);

/** Show a short help screen with options and commands. */
static void
dump_help(int exit_code)
{
	printf( "Usage: %s [options] EXPRESSION\n"
			"\n"
			"Search for packages in the index generated by update-eix.\n"
			"EXPRESSION is true or false. Packages for which the EXPRESSION gives true,\n"
			"are included in the final report.\n"
			"\n"
			"A EXPRESSION can be:\n"
			"    EXPRESSION [-o|-a] EXPRESSION\n"
			"    [local-options] PATTERN\n"
			"\n"
			"Global:\n"
			"   Exclusive options:\n"
			"     -h, --help            show this screen and exit\n"
			"     -V, --version         show version and exit\n"
			"     --dump                dump variables to stdout\n"
			"\n"
			"   Special:\n"
			"     -t  --test-non-matching Before other output, print non-matching entries\n"
			"                           of /etc/portage/package.*; this option is best\n"
			"                           combined with -T to clean up /etc/portage/package.*\n"
			"\n"
			"   Output:\n"
			"     -q, --quiet           no output of any kind\n"
			"     -n, --nocolor         do not use ANSI color codes\n"
			"     -F, --force-color     force colorful output\n"
			"     -c, --compact         compact search results\n"
			"     -v, --verbose         verbose search results\n"
			"     -l, --versionlines    print available versions line-by-line\n"
			"         --format          format string for normal output\n"
			"         --format-compact  format string for compact output\n"
			"         --format-verbose  format string for verbose output\n"
			"\n"
			"Local:\n"
			"  Miscellaneous:\n"
			"    -I, --installed       Next expression only matches installed packages.\n"
			"    -D  --dup-versions    Match packages with duplicated versions\n"
			"    -T  --test-redundancy Match packages with redundancy in\n"
			"                          /etc/portage/package.* according to the\n"
			"                          REDUNDANY* variables\n"
			"    -!, --not             Invert the expression.\n"
			"\n"
			"  Search Fields:\n"
			"    -S, --description     description\n"
			"    -A, --category-name   \"category/name\"\n"
			"    -C, --category        category\n"
			"    -s, --name            name (default)\n"
			"    -H, --homepage        homepage\n"
			"    -L, --license         license\n"
			"    -P, --provide         provides\n"
			"\n"
			"  Type of Pattern:\n"
			"    -r, --regex           Pattern is a regexp (default)\n"
			"    -e, --exact           Pattern is the exact string\n"
			"    -p, --pattern         Pattern is a wildcards-pattern\n"
			"    -f [m], --fuzzy [m]   Use fuzzy-search with a maximal levenshtein-distance m\n"
			"                          (default: "LEVENSHTEIN_DISTANCE_STR")\n"
			"\n"
			"You can contact the developers in #eix on irc.freenode.net or on\n"
			"the sourceforge-page "PACKAGE_BUGREPORT".\n"
			"There is also a wiki at "EIX_WIKI".\n"
			"This program is covered by the GNU General Public License. See COPYING for\n"
			"further information.\n",
		program_name);

	if(exit_code != -1) {
		exit(exit_code);
	}
}

/** On segfault: show some instructions to help us find the bug. */
void
sig_handler(int sig)
{
	if(sig == SIGSEGV)
		fprintf(stderr,
				"Received SIGSEGV - you probably found a bug in eix.\n"
				"Please proceed with the following few instructions and help us find the bug:\n"
				" * install gdb (sys-dev/gdb)\n"
				" * compile eix with FEATURES=\"nostrip\" CXXFLAGS=\"-g -ggdb3\"\n"
				" * enter gdb with \"gdb --args %s your_arguments_for_eix\"\n"
				" * type \"run\" and wait for the segfault to happen\n"
				" * type \"bt\" to get a backtrace (this helps us a lot)\n"
				" * post a bugreport and be sure to include the output from gdb ..\n"
				"\n"
				"Sorry for the inconvenience and thanks in advance!\n", program_name);
	exit(1);
}

/*	If you want to add a new parameter to eix just insert a line into
 *	long_options. If you only want a longopt, add a new define.
 *
 *	-- ebeinroth
 */

enum cli_options {
	O_FMT = 256,
	O_FMT_VERBOSE,
	O_FMT_COMPACT,
	O_EXC_OVERLAY,
	O_DUMP,
	O_COLOR,
	O_IGNORE_ETC_PORTAGE,
	O_USE,
	O_CURRENT,
	O_NORC
};

char *format_normal, *format_verbose, *format_compact;

PrintFormat format(get_package_property, print_package_property);

/** Local options for argument reading. */
static struct LocalOptions {
	bool be_quiet,
		 verbose_output,
		 compact_output,
		 show_help,
		 show_version,
		 dump_eixrc,
		 test_unused,
		 do_debug,
		 ignore_etc_portage,
		 is_current;
} rc_options;

/** Arguments and shortopts. */
static struct Option long_options[] = {
	// Global options
	Option("quiet",        'q',     Option::BOOLEAN,       &rc_options.be_quiet),

	Option("nocolor",      'n',     Option::BOOLEAN_T,     &format.no_color),
	Option("force-color",  'F',     Option::BOOLEAN_F,     &format.no_color),
	Option("versionlines", 'l',     Option::BOOLEAN_T,     &format.style_version_lines),

	Option("verbose",      'v',     Option::BOOLEAN,       &rc_options.verbose_output),
	Option("compact",      'c',     Option::BOOLEAN,       &rc_options.compact_output),
	Option("help",         'h',     Option::BOOLEAN_T,     &rc_options.show_help),
	Option("version",      'V',     Option::BOOLEAN_T,     &rc_options.show_version),
	Option("dump",         O_DUMP,  Option::BOOLEAN_T,     &rc_options.dump_eixrc),
	Option("test-non-matching",'t', Option::BOOLEAN_T,     &rc_options.test_unused),
	Option("debug",        'd',     Option::BOOLEAN_T,     &rc_options.do_debug),

	Option("is-current",   O_CURRENT, Option::BOOLEAN_T,   &rc_options.is_current),

	Option("ignore-etc-portage",  O_IGNORE_ETC_PORTAGE, Option::BOOLEAN_T,  &rc_options.ignore_etc_portage),

	Option("format",         O_FMT,         Option::STRING,   &format_normal),
	Option("format-verbose", O_FMT_VERBOSE, Option::STRING,   &format_verbose),
	Option("format-compact", O_FMT_COMPACT, Option::STRING,   &format_compact),

	// Options for criteria
	Option("installed",     'I'),
	Option("dup-versions",  'D'),
	Option("test-redundancy",'T'),
	Option("not",           '!'),

	// Algorithms for a criteria
	Option("fuzzy",         'f'),
	Option("regex",         'r'),
	Option("exact",         'e'),
	Option("pattern",       'p'),

	// What to match in this criteria
	Option("name",          's'),
	Option("category",      'C'),
	Option("category-name", 'A'),
	Option("description",   'S'),
	Option("license",       'L'),
	Option("homepage",      'H'),
	Option("provide",       'P'),

	// What to do with the next one
	Option("or",            'o'),
	Option("and",           'a'),

	Option(0 , 0)
};

/** Setup default values for all global variables. */
void
setup_defaults()
{
	EixRc &rc = get_eixrc();

	// Setup defaults
	(void) memset(&rc_options, 0, sizeof(rc_options));

	format_verbose             = (char*) rc["FORMAT_VERBOSE"].c_str();
	format_compact             = (char*) rc["FORMAT_COMPACT"].c_str();
	format_normal              = (char*) rc["FORMAT"].c_str();

	format.color_masked        = rc["COLOR_MASKED"];
	format.color_unstable      = rc["COLOR_UNSTABLE"];
	format.color_stable        = rc["COLOR_STABLE"];
	format.color_overlaykey    = rc["COLOR_OVERLAYKEY"];

	format.no_color            = !isatty(1) && !rc.getBool("FORCE_USECOLORS");
	format.mark_installed      = rc["MARK_INSTALLED"];
	format.style_version_lines = rc.getBool("STYLE_VERSION_LINES");
}

void
print_overlay_table(DBHeader &header)
{
	for(int i = 1;
		i < header.countOverlays();
		i++)
	{
		if( !format.no_color )
		{
			cout << format.color_overlaykey;
		}
		printf("[%i] ", i);
		if( !format.no_color )
		{
			cout << AnsiColor(AnsiColor::acDefault, 0);
		}
		cout << header.getOverlay(i) << endl;
	}
}

int
run_eix(int argc, char** argv)
{
	VarDbPkg varpkg_db("/var/db/pkg/");
	EixRc &eixrc = get_eixrc();

	// Setup defaults for all global variables like rc_options
	setup_defaults();

	// Read our options from the commandline.
	ArgumentReader argreader(argc, argv, long_options);

	// Only check if the versions uses the current layout
	if(rc_options.is_current) {
		return is_current_dbversion(EIX_CACHEFILE);
	}

	// Dump eixrc-stuff
	if(rc_options.dump_eixrc) {
		eixrc.dumpDefaults(stdout);
		exit(0);
	}

	// Show help screen
	if(rc_options.show_help) {
		dump_help(0);
	}

	// Show version
	if(rc_options.show_version) {
		dump_version(0);
	}

	// Honour a STFU
	if(rc_options.be_quiet) {
		close(1);
	}

	PortageSettings portagesettings;
	Matchatom *query = parse_cli(eixrc, varpkg_db, portagesettings, argreader.begin(), argreader.end());

	string varname;
	try {
		if(rc_options.verbose_output) {
			varname = "FORMAT_VERBOSE";
			format.setFormat(format_verbose);
		}
		else if(rc_options.compact_output) {
			varname = "FORMAT_COMPACT";
			format.setFormat(format_compact);
		}
		else {
			varname = "FORMAT";
			format.setFormat(format_normal);
		}
	}
	catch(ExBasic &e) {
		cout << "Problems while parsing " << varname << "." << endl
			<< e << endl;
		exit(1);
	}

	format.setupColors();

	eix::ptr_list<Package> matches;
	eix::ptr_list<Package> all_packages;


	/* Open database file */
	FILE *fp = fopen(EIX_CACHEFILE, "rb");
	if(!fp) {
		fprintf(stderr, "Can't open the database file "EIX_CACHEFILE" for reading (mode = 'rb')\n"
				"Did you forget to create it with 'update-eix'?\n");
		exit(1);
	}

	DBHeader header;

	io::read_header(fp, header);
	if(!header.isCurrent()) {
		fprintf(stderr, "Your database-file uses an obsolete format (%i, current is %i).\n"
				"Please run 'update-eix' and try again.\n", header.version, DBHeader::current);
		exit(1);
	}

	PackageReader reader(fp, header.size);
	while(reader.next())
	{
		if(query->match(&reader))
		{
			Package *release=reader.release();
			matches.push_back(release);
			if(rc_options.test_unused)
				all_packages.push_back(release);
		}
		else
		{
			if(rc_options.test_unused)
				all_packages.push_back(reader.release());
			else
				reader.skip();
		}
	}
	if(rc_options.test_unused)
	{
		print_unused("/etc/portage/package.keywords", all_packages);
		print_unused("/etc/portage/package.mask",     all_packages);
		print_unused("/etc/portage/package.unmask",   all_packages);
		print_unused("/etc/portage/package.use",      all_packages);
	}

	/* Sort the found matches by rating */
	if(FuzzyAlgorithm::sort_by_levenshtein()) {
		matches.sort(FuzzyAlgorithm::compare);
	}

	Keywords accepted_keywords;

	if(!eixrc.getBool("LOCAL_PORTAGE_CONFIG")) {
		rc_options.ignore_etc_portage = true;
		accepted_keywords = Keywords::KEY_STABLE;
	}
	else {
		accepted_keywords = portagesettings.getAcceptKeywords();
	}

	bool need_overlay_table = false;
	for(eix::ptr_list<Package>::iterator it = matches.begin();
		it != matches.end();
		++it)
	{
		/* Add individual maskings from this machines /etc/portage/ */
		if(!rc_options.ignore_etc_portage) {
			portagesettings.user_config->setMasks(*it);
			portagesettings.user_config->setStability(*it, accepted_keywords);
		}
		else {
			portagesettings.setStability(*it, accepted_keywords);
		}

		it->installed_versions = varpkg_db.getInstalledString(**it);

		if( !need_overlay_table
			&& (!it->have_same_overlay_key
				|| it->overlay_key != 0) )
		{
			need_overlay_table = true;
		}

		format.print(*it, &varpkg_db);
	}

	if(need_overlay_table)
	{
		print_overlay_table(header);
	}

	fputs("\n", stdout);
	printf("Found %i matches\n", matches.size());

	// Delete old query
	delete query;

	// Delete matches
	matches.delete_and_clear();

	return EXIT_SUCCESS;
}

int is_current_dbversion(const char *filename) {
	DBHeader header;
	FILE *fp = fopen(filename, "rb");
	if(!fp)
	{
		fprintf(stderr, "Can't open the database file %s for reading (mode = 'rb')\n"
				"Did you forget to create it with 'update-eix'?\n", filename);
		return 1;
	}
	io::read_header(fp, header);
	fclose(fp);

	return header.isCurrent() ? 0 : 1;
}

void print_unused(const char *filename, const eix::ptr_list<Package> &packagelist)
{
	vector<string> unused;
	vector<string> lines;
	pushback_lines(filename, &lines, false, true);
	for(unsigned int i = 0;
		i<lines.size();
		i++)
	{
		if(lines[i].size() == 0)
		{
			continue;
		}
		KeywordMask *m = NULL;

		try {
			string::size_type n = lines[i].find_first_of("\t ");
			if(n == string::npos) {
				m = new KeywordMask(lines[i].c_str());
			}
			else {
				m = new KeywordMask(lines[i].substr(0, n).c_str());
			}
		}
		catch(ExBasic e) {
			portage_parse_error(filename, i, lines[i], e);
		}
		if(!m)
			continue;

		eix::ptr_list<Package>::const_iterator pi;
		for(pi = packagelist.begin(); pi != packagelist.end(); ++pi)
		{
			if(m->ismatch(**pi))
				break;
		}
		if(pi == packagelist.end())
		{
			unused.push_back(lines[i]);
		}
		delete m;
	}
	cout << "Entries in " << filename << " matching no existing version:\n\n";
	for(vector<string>::iterator it=unused.begin();
		it != unused.end(); it++)
		cout << *it << endl;
	cout << "--\n\n";
}

int main(int argc, char** argv)
{
	program_name = argv[0];

	/* Install signal handler */
	signal(SIGSEGV, sig_handler);

	int ret = 0;
	try {
		ret = run_eix(argc, argv);
	}
	catch(ExBasic e) {
		cout << e << endl;
		return 1;
	}
	return ret;
}
