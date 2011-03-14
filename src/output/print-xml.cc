// vim:set noet cinoptions= sw=4 ts=4:
// This file is part of the eix project and distributed under the
// terms of the GNU General Public License v2.
//
// Copyright (c)
//   Bob Shaffer II <bob.shaffer.2 at gmail.com>
//   Martin Väth <vaeth@mathematik.uni-wuerzburg.de>

#include "print-xml.h"
#include <database/header.h>
#include <eixTk/likely.h>
#include <eixTk/sysutils.h>
#include <eixrc/eixrc.h>
#include <portage/extendedversion.h>
#include <portage/instversion.h>
#include <portage/keywords.h>
#include <portage/overlay.h>
#include <portage/package.h>
#include <portage/set_stability.h>
#include <portage/vardbpkg.h>

#include <set>
#include <string>
#include <vector>
#include <iostream>

#include <cstddef>

using namespace std;

const PrintXml::XmlVersion PrintXml::current;

void
PrintXml::runclear()
{
	started = false;
	curcat.clear();
	count = 0;
}

void
PrintXml::clear(EixRc *eixrc)
{
	if(unlikely(eixrc == NULL)) {
		print_overlay = false;
		keywords_mode = KW_NONE;
		dateformat = "%s";
	}
	else {
		dateformat = (*eixrc)["XML_DATE"];
		print_overlay = eixrc->getBool("XML_OVERLAY");
		static const char *values[] = {
			"none",
			"both",
			"effective",
			"effective*",
			"full",
			"full*",
			NULL };
		switch(eixrc->getBoolTextlist("XML_KEYWORDS", values)) {
			case 0:
			case -1: keywords_mode = KW_NONE;  break;
			case -2: keywords_mode = KW_BOTH;  break;
			case -3: keywords_mode = KW_EFF;   break;
			case -4: keywords_mode = KW_EFFS;  break;
			case -5: keywords_mode = KW_FULL;  break;
			default: keywords_mode = KW_FULLS; break;
		}
	}
	runclear();
}

void
PrintXml::start()
{
	if(unlikely(started))
		return;
	started = true;

	cout << "<?xml version='1.0' encoding='UTF-8'?>\n"
		"<eixdump version=\"" << current << "\">\n";
}

void
PrintXml::finish()
{
	if(!started)
		return;

	if (count) {
		cout << "\t</category>\n";
	}
	cout << "</eixdump>\n";

	runclear();
}

static void
print_iuse(const set<IUse> &s, IUse::Flags wanted, const char *dflt)
{
	bool have_found(false);
	for(set<IUse>::const_iterator it(s.begin()); likely(it != s.end()); ++it) {
		if(!((it->flags) & wanted))
			continue;
		if(likely(have_found)) {
			cout << " " << it->name();
			continue;
		}
		have_found = true;
		if(dflt != NULL)
			cout << "\t\t\t\t<iuse default=\"" << dflt << "\">";
		else
			cout << "\t\t\t\t<iuse>";
		cout << it->name();
	}
	if(have_found)
		cout << "</iuse>\n";
}

void
PrintXml::package(Package *pkg)
{
	if(unlikely(!started))
		start();
	if(unlikely(curcat != pkg->category)) {
		if (!curcat.empty()) {
			cout << "\t</category>\n";
		}
		curcat = pkg->category;
		cout << "\t<category name=\"" << escape_string(curcat) << "\">\n";
	}
	// category, name, desc, homepage, licenses, provide;
	cout << "\t\t<package name=\"" << escape_string(pkg->name) << "\">\n"
		"\t\t\t<description>" << escape_string(pkg->desc) << "</description>\n"
		"\t\t\t<homepage>" << escape_string(pkg->homepage) << "</homepage>\n"
		"\t\t\t<licenses>" << escape_string(pkg->licenses) << "</licenses>\n";
	if(!pkg->provide.empty()) {
		cout << "\t\t\t<provide>" << escape_string(pkg->get_provide()) << "</provide>\n";
	}

	set<const Version*> have_inst;
	if((likely(var_db_pkg != NULL)) && var_db_pkg->isInstalled(*pkg)) {
		set<BasicVersion> know_inst;
		// First we check which versions are installed with correct overlays.
		if(likely(hdr != NULL)) {
			// Package is a 'list' of Versions with added members ^^
			for(Package::const_iterator ver(pkg->begin());
				likely(ver != pkg->end()); ++ver) {
				if(var_db_pkg->isInstalledVersion(*pkg, *ver, *hdr, portdir.c_str()) > 0) {
					know_inst.insert(**ver);
					have_inst.insert(*ver);
				}
			}
		}
		// From the remaining ones we choose the last.
		// The following should actually be const_reverse_iterator,
		// but some compilers would then need a cast of rend(),
		// see http://bugs.gentoo.org/show_bug.cgi?id=354071
		for(Package::reverse_iterator ver(pkg->rbegin());
			likely(ver != pkg->rend()); ++ver) {
			if(know_inst.find(**ver) == know_inst.end()) {
				know_inst.insert(**ver);
				have_inst.insert(*ver);
			}
		}
	}

	for(Package::const_iterator ver(pkg->begin()); likely(ver != pkg->end()); ++ver) {
		bool versionInstalled(false);
		InstVersion *installedVersion(NULL);
		if(have_inst.find(*ver) != have_inst.end()) {
			if(var_db_pkg->isInstalled(*pkg, *ver, &installedVersion)) {
				versionInstalled = true;
				var_db_pkg->readInstDate(*pkg, *installedVersion);
			}
		}

		cout << "\t\t\t<version id=\"" << escape_string(ver->getFull()) << "\"";
		ExtendedVersion::Overlay overlay_key(ver->overlay_key);
		if(unlikely(overlay_key != 0)) {
			const OverlayIdent &overlay(hdr->getOverlay(overlay_key));
			if((print_overlay || overlay.label.empty()) && !(overlay.path.empty())) {
				cout << " overlay=\"" << escape_string(overlay.path) << "\"";
			}
			if(!overlay.label.empty()) {
				cout << " repository=\"" << escape_string(overlay.label) << "\"";
			}
		}
		if (!ver->slotname.empty()) {
			cout << " slot=\"" << escape_string(ver->slotname) << "\"";
		}
		if (versionInstalled) {
			cout << " installed=\"1\" installDate=\"" <<
				escape_string(date_conv(dateformat.c_str(), installedVersion->instDate)) << "\"";
		}
		cout << ">\n";

		MaskFlags currmask(ver->maskflags);
		KeywordsFlags currkey(ver->keyflags);
		MaskFlags wasmask;
		KeywordsFlags waskey;
		stability->calc_version_flags(false, wasmask, waskey, *ver, pkg);

		vector<string> mask_text, unmask_text;
		if(wasmask.isHardMasked()) {
			if(currmask.isProfileMask()) {
				mask_text.push_back("profile");
			}
			else if(currmask.isPackageMask()) {
				mask_text.push_back("hard");
			}
			else if(wasmask.isProfileMask()) {
				mask_text.push_back("profile");
				unmask_text.push_back("package_unmask");
			}
			else {
				mask_text.push_back("hard");
				unmask_text.push_back("package_unmask");
			}
		}
		else if(currmask.isHardMasked()) {
			mask_text.push_back("package_mask");
		}

		if(currkey.isStable()) {
			if (waskey.isStable()) {
				//
			}
			else if (waskey.isUnstable()) {
				mask_text.push_back("keyword");
				unmask_text.push_back("package_keywords");
			}
			else if (waskey.isMinusKeyword()) {
				mask_text.push_back("minus_keyword");
				unmask_text.push_back("package_keywords");
			}
			else if (waskey.isAlienStable()) {
				mask_text.push_back("alien_stable");
				unmask_text.push_back("package_keywords");
			}
			else if (waskey.isAlienUnstable()) {
				mask_text.push_back("alien_unstable");
				unmask_text.push_back("package_keywords");
			}
			else if (waskey.isMinusAsterisk()) {
				mask_text.push_back("minus_asterisk");
				unmask_text.push_back("package_keywords");
			}
			else {
				mask_text.push_back("missing_keyword");
				unmask_text.push_back("package_keywords");
			}
		}
		else if (currkey.isUnstable()) {
			mask_text.push_back("keyword");
		}
		else if (currkey.isMinusKeyword()) {
			mask_text.push_back("minus_keyword");
		}
		else if (currkey.isAlienStable()) {
			mask_text.push_back("alien_stable");
		}
		else if (currkey.isAlienUnstable()) {
			mask_text.push_back("alien_unstable");
		}
		else if (currkey.isMinusAsterisk()) {
			mask_text.push_back("minus_asterisk");
		}
		else {
			mask_text.push_back("missing_keyword");
		}

		for(vector<string>::const_iterator it(mask_text.begin());
			unlikely(it != mask_text.end()); ++it) {
			cout << "\t\t\t\t<mask type=\"" << *it << "\" />\n";
		}

		for(vector<string>::const_iterator it(unmask_text.begin());
			unlikely(it != unmask_text.end()); ++it) {
			cout << "\t\t\t\t<unmask type=\"" << *it << "\" />\n";
		}

		if (!(ver->iuse.empty())) {
			//cout << "\t\t\t\t<iuse>" << ver->iuse.asString() << "</iuse>\n";
			const set<IUse> &s(ver->iuse.asSet());
			print_iuse(s, IUse::USEFLAGS_NORMAL, NULL);
			print_iuse(s, IUse::USEFLAGS_PLUS, "1");
			print_iuse(s, IUse::USEFLAGS_MINUS, "-1");
		}
		if (versionInstalled) {
			string iuse_disabled, iuse_enabled;
			var_db_pkg->readUse(*pkg, *installedVersion);
			vector<string> inst_iuse = installedVersion->inst_iuse;
			set<string> usedUse = installedVersion->usedUse;
			for (vector<string>::iterator iu = inst_iuse.begin(); iu != inst_iuse.end(); iu++) {
				if (usedUse.find(*iu) == usedUse.end()) {
					if (!iuse_disabled.empty())
						iuse_disabled.append(1, ' ');
					iuse_disabled.append(*iu);
				} else {
					if (!iuse_enabled.empty())
						iuse_enabled.append(1, ' ');
					iuse_enabled.append(*iu);
				}
			}
			if (!iuse_disabled.empty()) {
				cout << "\t\t\t\t<use enabled=\"0\">" << iuse_disabled << "</use>\n";
			}
			if (!iuse_enabled.empty()) {
				cout << "\t\t\t\t<use enabled=\"1\">" << iuse_enabled << "</use>\n";
			}
		}

		ExtendedVersion::Restrict restrict(ver->restrictFlags);
		if(unlikely(restrict != ExtendedVersion::RESTRICT_NONE)) {
			if(unlikely(restrict & ExtendedVersion::RESTRICT_BINCHECKS)) {
				cout << "\t\t\t\t<restrict flag=\"binchecks\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_STRIP)) {
				cout << "\t\t\t\t<restrict flag=\"strip\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_TEST)) {
				cout << "\t\t\t\t<restrict flag=\"test\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_USERPRIV)) {
				cout << "\t\t\t\t<restrict flag=\"userpriv\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_INSTALLSOURCES)) {
				cout << "\t\t\t\t<restrict flag=\"installsources\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_FETCH)) {
				cout << "\t\t\t\t<restrict flag=\"fetch\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_MIRROR)) {
				cout << "\t\t\t\t<restrict flag=\"mirror\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_PRIMARYURI)) {
				cout << "\t\t\t\t<restrict flag=\"primaryuri\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_BINDIST)) {
				cout << "\t\t\t\t<restrict flag=\"bindist\" />\n";
			}
			if(unlikely(restrict & ExtendedVersion::RESTRICT_PARALLEL)) {
				cout << "\t\t\t\t<restrict flag=\"parallel\" />\n";
			}
		}
		ExtendedVersion::Restrict properties(ver->propertiesFlags);
		if(unlikely(properties != ExtendedVersion::PROPERTIES_NONE)) {
			if(unlikely(properties & ExtendedVersion::PROPERTIES_INTERACTIVE)) {
				cout << "\t\t\t\t<properties flag=\"interactive\" />\n";
			}
			if(unlikely(properties & ExtendedVersion::PROPERTIES_LIVE)) {
				cout << "\t\t\t\t<properties flag=\"live\" />\n";
			}
			if(unlikely(properties & ExtendedVersion::PROPERTIES_VIRTUAL)) {
				cout << "\t\t\t\t<properties flag=\"virtual\" />\n";
			}
			if(unlikely(properties & ExtendedVersion::PROPERTIES_SET)) {
				cout << "\t\t\t\t<properties flag=\"set\" />\n";
			}
		}

		if(keywords_mode != KW_NONE) {
			bool print_full(keywords_mode != KW_EFF);
			bool print_effective(keywords_mode != KW_FULL);
			string full_kw, eff_kw;
			if(print_full)
				full_kw = ver->get_full_keywords();
			if(print_effective)
				eff_kw = ver->get_effective_keywords();
			if((keywords_mode == KW_FULLS) || (keywords_mode == KW_EFFS)) {
				if(likely(full_kw == eff_kw)) {
					if(keywords_mode == KW_FULLS)
						print_effective = false;
					else
						print_full = false;
				}
			}
			if(print_full)
				cout << "\t\t\t\t<keywords>" << escape_string(full_kw) << "</keywords>\n";
			if(print_effective)
				cout << "\t\t\t\t<effective_keywords>" << escape_string(eff_kw) << "</effective_keywords>\n";
		}
		cout << "\t\t\t</version>\n";
	}
	cout << "\t\t</package>\n";
	++count;
}

string
PrintXml::escape_string(const string &s)
{
	string ret;
	string::size_type prev(0);
	string::size_type len(s.length());
	for(string::size_type i(0); likely(i < len); ++i) {
		const char *replace;
		switch(s[i]) {
			case '&': replace = "&amp;"; break;
			case '<': replace = "&lt;"; break;
			case '>': replace = "&gt;"; break;
			case '\'': replace = "&apos;"; break;
			case '\"': replace = "&quot;"; break;
			default: replace = NULL; break;
		}
		if(unlikely(replace != NULL)) {
			ret.append(s, prev, i - prev);
			ret.append(replace);
			prev = i + 1;
		}
	}
	if(likely(prev == 0))
		return s;
	if(prev < len)
		ret.append(s, prev, len - prev);
	return ret;
}
