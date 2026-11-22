// Invaders wear the gear they rolled (#1708). An Invader's look is 16 part bytes in its UnitData at +0x08 (HD TR LG RA LA
// RH LH SH S1..S8, 1.13c layout), each an index into that part's monstats2 list (HDv, TRv, ...). Dress() turns the base
// items a spawn rolled into those bytes:
//   a helm, circlet or pelt -> HD, by the item's alternategfx (weapons.txt / armor.txt), as a player shows it
//   body armor              -> TR LG RA LA S1 S2, by its Torso Legs rArm lArm rSPad lSPad levels (those lists are lit,med,hvy)
//   a weapon                -> the class's weapon hand (the Amazon's bow is in LH), a second weapon -> LH
//   a shield -> SH, a Necromancer head -> S3 (the player Necromancer's head layer); quivers, rings and the rest show nothing
// The art is the class's player art in its one monstats2 weapon class (BaseW), so a token with no art there shows the
// nearest one that has (Nearest), and a part with no list (the Sorceress's shield, the Amazon's quiver) is not shown.
// Items and Classes must match the tables: tools/tests/test_invader_dress.py checks them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pd2::invader_dress {

constexpr int PartBytes = 16;
enum Part : int { HD = 0, TR, LG, RA, LA, RH, LH, SH, S1, S2, S3 };
enum class Slot : uint8_t { Head, Body, Weapon, Shield, Skull };

struct Item {
	const char* code;
	Slot slot;
	const char* tok;  // alternategfx
	uint8_t body[6];  // Torso Legs rArm lArm rSPad lSPad (body armor only)
};

// every base item an Invader* TC rolls that shows (gloves, boots, belts, rings, amulets and quivers do not)
constexpr Item Items[] = {
	{"uul", Slot::Body, "ful", {2, 2, 2, 2, 2, 2}},
	{"uea", Slot::Body, "lea", {1, 0, 0, 0, 1, 1}},
	{"ula", Slot::Body, "hla", {1, 0, 1, 1, 1, 1}},
	{"utu", Slot::Body, "stu", {0, 1, 1, 0, 1, 1}},
	{"urs", Slot::Body, "brs", {2, 0, 0, 0, 2, 2}},
	{"utp", Slot::Body, "ltp", {1, 1, 2, 0, 2, 2}},
	{"ung", Slot::Body, "rng", {1, 1, 0, 0, 1, 1}},
	{"ucl", Slot::Body, "scl", {1, 1, 1, 1, 1, 1}},
	{"uhn", Slot::Body, "chn", {1, 1, 1, 1, 2, 2}},
	{"upl", Slot::Body, "spl", {2, 1, 1, 1, 1, 1}},
	{"ult", Slot::Body, "plt", {2, 2, 2, 2, 1, 1}},
	{"uar", Slot::Body, "aar", {2, 2, 1, 2, 2, 0}},
	{"uth", Slot::Body, "gth", {1, 2, 2, 2, 2, 2}},
	{"uld", Slot::Body, "fld", {2, 2, 1, 1, 2, 2}},
	{"uap", Slot::Head, "cap"},
	{"ukp", Slot::Head, "skp"},
	{"ulm", Slot::Head, "hlm"},
	{"uhl", Slot::Head, "fhl"},
	{"usk", Slot::Head, "msk"},
	{"uh9", Slot::Head, "bhm"},
	{"uhm", Slot::Head, "ghm"},
	{"urn", Slot::Head, "crn"},
	{"ci1", Slot::Head, "lit"},
	{"ci2", Slot::Head, "lit"},
	{"ci3", Slot::Head, "lit"},
	{"bab", Slot::Head, "ba1"},
	{"bac", Slot::Head, "ba1"},
	{"bad", Slot::Head, "ba3"},
	{"bae", Slot::Head, "ba5"},
	{"baf", Slot::Head, "ba5"},
	{"drb", Slot::Head, "dr1"},
	{"drc", Slot::Head, "dr4"},
	{"drd", Slot::Head, "dr3"},
	{"dre", Slot::Head, "dr4"},
	{"drf", Slot::Head, "dr1"},
	{"uuc", Slot::Shield, "buc"},
	{"uml", Slot::Shield, "buc"},
	{"urg", Slot::Shield, "lrg"},
	{"upk", Slot::Shield, "spk"},
	{"uit", Slot::Shield, "kit"},
	{"ush", Slot::Shield, "bsh"},
	{"uow", Slot::Shield, "tow"},
	{"uts", Slot::Shield, "kit"},
	{"pab", Slot::Shield, "pa1"},
	{"pac", Slot::Shield, "pa1"},
	{"pad", Slot::Shield, "pa3"},
	{"pae", Slot::Shield, "pa3"},
	{"paf", Slot::Shield, "pa5"},
	{"neb", Slot::Skull, "ne1"},
	{"neg", Slot::Skull, "ne2"},
	{"ned", Slot::Skull, "ne3"},
	{"nee", Slot::Skull, "ne3"},
	{"nef", Slot::Skull, "ne2"},
	{"6sb", Slot::Weapon, "sbw"},
	{"6hb", Slot::Weapon, "sbw"},
	{"6lb", Slot::Weapon, "lbw"},
	{"6cb", Slot::Weapon, "lbw"},
	{"6s7", Slot::Weapon, "sbb"},
	{"6l7", Slot::Weapon, "lbb"},
	{"6sw", Slot::Weapon, "sbb"},
	{"6lw", Slot::Weapon, "lbb"},
	{"amb", Slot::Weapon, "am1"},
	{"amc", Slot::Weapon, "am2"},
	{"6lx", Slot::Weapon, "lxb"},
	{"6mx", Slot::Weapon, "lxb"},
	{"6hx", Slot::Weapon, "hxb"},
	{"6rx", Slot::Weapon, "hxb"},
	{"7sr", Slot::Weapon, "spr"},
	{"7tr", Slot::Weapon, "tri"},
	{"7br", Slot::Weapon, "brn"},
	{"7st", Slot::Weapon, "tri"},
	{"7p7", Slot::Weapon, "pik"},
	{"amd", Slot::Weapon, "am3"},
	{"ame", Slot::Weapon, "pik"},
	{"ama", Slot::Weapon, "pil"},
	{"amf", Slot::Weapon, "pil"},
	{"7ar", Slot::Weapon, "ktr"},
	{"7wb", Slot::Weapon, "ktr"},
	{"7xf", Slot::Weapon, "axf"},
	{"7cs", Slot::Weapon, "axf"},
	{"7lw", Slot::Weapon, "clw"},
	{"7tw", Slot::Weapon, "clw"},
	{"7qr", Slot::Weapon, "skr"},
	{"7dg", Slot::Weapon, "dgr"},
	{"7di", Slot::Weapon, "dir"},
	{"7kr", Slot::Weapon, "dir"},
	{"7bl", Slot::Weapon, "dgr"},
	{"7tk", Slot::Weapon, "dgr"},
	{"7ta", Slot::Weapon, "hax"},
	{"7bk", Slot::Weapon, "dgr"},
	{"7b8", Slot::Weapon, "hax"},
	{"7ja", Slot::Weapon, "jav"},
	{"7pi", Slot::Weapon, "pil"},
	{"7s7", Slot::Weapon, "jav"},
	{"7gl", Slot::Weapon, "glv"},
	{"7ts", Slot::Weapon, "pil"},
	{"7ha", Slot::Weapon, "hax"},
	{"7ax", Slot::Weapon, "axe"},
	{"72a", Slot::Weapon, "axe"},
	{"7mp", Slot::Weapon, "axe"},
	{"7wa", Slot::Weapon, "hax"},
	{"7ma", Slot::Weapon, "mac"},
	{"7mt", Slot::Weapon, "mac"},
	{"7fl", Slot::Weapon, "fla"},
	{"7wh", Slot::Weapon, "whm"},
	{"7ss", Slot::Weapon, "ssd"},
	{"7sm", Slot::Weapon, "scm"},
	{"7sb", Slot::Weapon, "scm"},
	{"7fc", Slot::Weapon, "flc"},
	{"7cr", Slot::Weapon, "crs"},
	{"7bs", Slot::Weapon, "bsd"},
	{"7ls", Slot::Weapon, "lsd"},
	{"7wd", Slot::Weapon, "flc"},
	{"7cm", Slot::Weapon, "clm"},
	{"7gs", Slot::Weapon, "gsd"},
	{"7b7", Slot::Weapon, "clm"},
	{"7fb", Slot::Weapon, "clm"},
	{"7gd", Slot::Weapon, "gsd"},
	{"7la", Slot::Weapon, "lax"},
	{"7ba", Slot::Weapon, "lax"},
	{"7bt", Slot::Weapon, "btx"},
	{"7ga", Slot::Weapon, "btx"},
	{"7gi", Slot::Weapon, "gix"},
	{"7m7", Slot::Weapon, "mau"},
	{"7gm", Slot::Weapon, "mau"},
	{"7o7", Slot::Weapon, "hal"},
	{"7vo", Slot::Weapon, "hal"},
	{"7s8", Slot::Weapon, "scy"},
	{"7pa", Slot::Weapon, "hal"},
	{"7h7", Slot::Weapon, "pax"},
	{"7wc", Slot::Weapon, "scy"},
	{"7cl", Slot::Weapon, "clb"},
	{"7sp", Slot::Weapon, "clb"},
	{"7wn", Slot::Weapon, "wnd"},
	{"7yw", Slot::Weapon, "ywn"},
	{"7bw", Slot::Weapon, "bwn"},
	{"7gw", Slot::Weapon, "bwn"},
	{"6ss", Slot::Weapon, "bst"},
	{"6ls", Slot::Weapon, "sst"},
	{"6cs", Slot::Weapon, "cst"},
	{"6bs", Slot::Weapon, "sst"},
	{"6ws", Slot::Weapon, "lst"},
	{"obb", Slot::Weapon, "ob1"},
	{"obc", Slot::Weapon, "ob1"},
	{"obd", Slot::Weapon, "ob3"},
	{"obe", Slot::Weapon, "ob4"},
	{"obf", Slot::Weapon, "ob4"},
	{"7sc", Slot::Weapon, "mac"},
	{"7qs", Slot::Weapon, "mac"},
	{"7ws", Slot::Weapon, "whm"},
};

// each class's monstats2 lists (InvaderAmazon..InvaderSorceress, ClassNames order) for the parts Dress sets by token;
// TR LG RA LA S1 S2 are lit,med,hvy for every class
struct Lists {
	const char* hd;
	const char* rh;
	const char* lh;
	const char* sh;
	const char* s3;
	Part hand;  // the first weapon's part
};
constexpr Lists Classes[7] = {
	{"lit", "", "am1,am2,lbb,lbw,sbb,sbw", "", "", LH},
	{"ghm,cap,skp,hlm,fhl,msk,bhm,crn,lit", "axf,ktr,clw,skr", "axf,ktr,clw,skr", "", "", RH},
	{"ba5,ba1,ba3", "axe,bsd,gsd,lsd,whm,hax,mac,fla,ssd,scm,flc,crs,clm", "axe,bsd,gsd,lsd,whm,hax,mac,fla,ssd,scm,flc,crs,clm", "", "", RH},
	{"dr3,dr1,dr4", "clb,hax", "", "bsh,buc,lrg,spk,kit,tow", "", RH},
	{"msk,cap,skp,hlm,fhl,bhm,ghm,crn,lit", "bwn,ywn,wnd", "", "", "ne2,ne1,ne3", RH},
	{"crn,cap,skp,hlm,fhl,msk,bhm,ghm,lit", "fla,crs,ssd,scm,flc,bsd,lsd,mac,whm,hax,axe", "", "pa3,pa1,pa5", "", RH},
	{"lit", "cst,lst,sst,bst", "", "", "", RH},
};

// a token with no art in the class's weapon class -> the nearest that has (the Amazon's spear and javelin have none)
struct Near {
	int cls;
	const char* from;
	const char* to;
};
constexpr Near Nearest[] = {
	{0, "lxb", "lbw"}, {0, "hxb", "lbw"},  // a crossbow: the long bow
	{1, "dgr", "ktr"}, {1, "dir", "ktr"},  // a dagger: the katar
	{2, "lax", "axe"}, {2, "btx", "axe"}, {2, "gix", "axe"}, {2, "hal", "axe"}, {2, "pax", "axe"}, {2, "scy", "axe"},  // two-handed axes, polearms
	{2, "mau", "whm"},  // a maul: the war hammer
	{2, "spr", "lsd"}, {2, "tri", "lsd"}, {2, "brn", "lsd"}, {2, "pik", "lsd"}, {2, "jav", "lsd"}, {2, "pil", "lsd"}, {2, "glv", "lsd"},  // spears, javelins
	{2, "dgr", "ssd"},  // a throwing knife: the short sword
	{6, "ob1", "sst"}, {6, "ob3", "sst"}, {6, "ob4", "sst"},  // an orb: the short staff
};

// The index of `tok` in the comma list, -1 when it is not there
inline int IndexOf(const char* list, const char* tok) noexcept {
	const size_t n = std::strlen(tok);
	for (int i = 0; *list != '\0'; ++i) {
		const char* end = std::strchr(list, ',');
		const size_t len = end != nullptr ? static_cast<size_t>(end - list) : std::strlen(list);
		if (len == n && std::strncmp(list, tok, n) == 0) {
			return i;
		}
		if (end == nullptr) {
			break;
		}
		list = end + 1;
	}
	return -1;
}

// The item of a D2R item code (4 bytes, a 3-letter code padded with a space), nullptr when it shows nothing
inline const Item* Find(uint32_t code) noexcept {
	char c[5] = {};
	std::memcpy(c, &code, 4);
	if (c[3] == ' ') {
		c[3] = '\0';
	}
	for (const Item& it : Items) {
		if (std::strcmp(it.code, c) == 0) {
			return &it;
		}
	}
	return nullptr;
}

struct Result {
	int exact = 0, nearest = 0, unshown = 0;
};

// Sets the part bytes of class `cls` from the base items it rolled, in roll order. A part with nothing to show keeps its byte.
inline Result Dress(int cls, const uint32_t* codes, int n, uint8_t* parts) noexcept {
	Result r;
	if (cls < 0 || cls >= 7 || parts == nullptr) {
		return r;
	}
	const Lists& l = Classes[cls];
	const auto set = [&](Part part, const char* list, const char* tok) noexcept {
		int i = IndexOf(list, tok);
		if (i >= 0) {
			r.exact += 1;
		} else {
			for (const Near& m : Nearest) {
				if (m.cls == cls && std::strcmp(m.from, tok) == 0) {
					i = IndexOf(list, m.to);
				}
			}
			(i >= 0 ? r.nearest : r.unshown) += 1;
		}
		if (i >= 0) {
			parts[part] = static_cast<uint8_t>(i);
		}
	};
	const char* firstWeapon = nullptr;
	int weapons = 0;
	for (int k = 0; k < n; ++k) {
		const Item* it = Find(codes[k]);
		if (it == nullptr) {
			continue;
		}
		switch (it->slot) {
		case Slot::Head:
			set(HD, l.hd, it->tok);
			break;
		case Slot::Body:
			for (int p = 0; p < 6; ++p) {
				parts[p < 4 ? TR + p : S1 + (p - 4)] = it->body[p];
			}
			r.exact += 1;
			break;
		case Slot::Weapon:
			if (weapons++ == 0) {
				firstWeapon = it->tok;
				set(l.hand, l.hand == LH ? l.lh : l.rh, it->tok);
			} else if (l.hand == RH && *l.lh != '\0') {
				set(LH, l.lh, it->tok);
			} else {
				r.unshown += 1;
			}
			break;
		case Slot::Shield:
			if (*l.sh != '\0') {
				set(SH, l.sh, it->tok);
			} else {
				r.unshown += 1;
			}
			break;
		case Slot::Skull:
			if (*l.s3 != '\0') {
				set(S3, l.s3, it->tok);
			} else {
				r.unshown += 1;
			}
			break;
		}
	}
	if (weapons == 1 && l.hand == RH && *l.lh != '\0') {
		set(LH, l.lh, firstWeapon);  // one two-hander in a dual-wield pose: the same in both hands, the nearest look
	}
	return r;
}

}  // namespace pd2::invader_dress
