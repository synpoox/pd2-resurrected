// Invaders wear the gear they rolled (#1708). An Invader's look is 16 part bytes in its UnitData at +0x08 (HD TR LG RA LA
// RH LH SH S1..S8, 1.13c layout), each an index into that part's monstats2 list (HDv, TRv, ...). Dress() turns the base
// items a spawn rolled into those bytes:
//   a helm, circlet or pelt -> HD, by the item's alternategfx (weapons.txt / armor.txt), as a player shows it
//   body armor              -> TR LG RA LA S1 S2, by its Torso Legs rArm lArm rSPad lSPad levels (those lists are lit,med,hvy)
//   a weapon                -> the class's weapon hand (the Amazon's bow LH, her spear RH), a second weapon -> LH
//   a shield -> SH, a Necromancer head -> S3 (the player Necromancer's head layer); quivers, rings and the rest show nothing
// Every token is the item's own, except a base with no art in the class's one pose: it shows as the nearest (Nearest).
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
// TR LG RA LA S1 S2 are lit,med,hvy for every class. `nil` is the vanilla empty token (no layer, no attachment): a hand
// or shield the roll left empty shows nothing, never the spawn's random token.
struct Lists {
	const char* hd;
	const char* rh;
	const char* lh;
	const char* sh;
	const char* s3;
	Part hand;  // the first weapon's part when its token is in both hands' lists or in neither
};
constexpr Lists Classes[7] = {
	{"lit", "nil,spr,tri,brn,pik,am3,pil", "am1,am2,lbb,lbw,sbb,sbw,lxb,hxb,nil", "nil,buc,lrg,spk,kit,bsh,tow", "", LH},
	{"ghm,cap,skp,hlm,fhl,msk,bhm,crn,lit", "axf,ktr,clw,skr,dgr,dir", "axf,ktr,clw,skr,dgr,dir", "", "", RH},
	{"ba5,ba1,ba3", "axe,bsd,gsd,lsd,whm,hax,mac,fla,ssd,scm,flc,crs,clm,lax,btx,gix,mau,hal,pax,scy,spr,tri,brn,pik,dgr,jav,pil,glv",
	 "axe,bsd,gsd,lsd,whm,hax,mac,fla,ssd,scm,flc,crs,clm,lax,btx,gix,mau,hal,pax,scy,spr,tri,brn,pik,dgr,jav,pil,glv,nil", "", "", RH},
	{"dr3,dr1,dr4", "clb,hax", "", "bsh,buc,lrg,spk,kit,tow", "", RH},
	{"msk,cap,skp,hlm,fhl,bhm,ghm,crn,lit", "bwn,ywn,wnd", "", "", "ne2,ne1,ne3", RH},
	{"crn,cap,skp,hlm,fhl,msk,bhm,ghm,lit", "fla,crs,ssd,scm,flc,bsd,lsd,mac,whm,hax,axe", "", "pa3,pa1,pa5", "", RH},
	{"lit", "cst,lst,sst,bst,ob1,ob3,ob4", "", "nil,buc,lrg,spk,kit,bsh,tow", "", RH},
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
	int shown = 0, unshown = 0;
};

// Sets the part bytes of class `cls` from the base items it rolled, in roll order: the hands and the shield start empty
// (nil), each item then shows its own token. An item whose part has no list is counted as not shown.
// D2R draws an Invader in its player art in the class's one monstats2 weapon class (BaseW), and when that pose has no
// legacy DCC for a token it draws the hand empty, in HD and legacy alike (Slot4, #1708). Such a token is set as the
// nearest token that has one. tools/tests/test_invader_dress.py checks every listed token has legacy art or a row here.
struct Near {
	const char* cls;
	const char* from;
	const char* to;
};
constexpr Near Nearest[] = {
	{"AM", "lxb", "lbw"}, {"AM", "hxb", "lbw"},  // a crossbow in the bow pose: the long bow
	{"AI", "dgr", "ktr"}, {"AI", "dir", "ktr"},  // a dagger in the claw pose: the katar
	{"BA", "lax", "axe"}, {"BA", "btx", "axe"}, {"BA", "gix", "axe"}, {"BA", "hal", "axe"}, {"BA", "pax", "axe"}, {"BA", "scy", "axe"},
	{"BA", "mau", "whm"}, {"BA", "dgr", "ssd"},
	{"BA", "spr", "lsd"}, {"BA", "tri", "lsd"}, {"BA", "brn", "lsd"}, {"BA", "pik", "lsd"}, {"BA", "jav", "lsd"}, {"BA", "pil", "lsd"},
	{"BA", "glv", "lsd"},  // two-handers, spears, javelins and knives in the dual-wield pose
	{"SO", "ob1", "sst"}, {"SO", "ob3", "sst"}, {"SO", "ob4", "sst"},  // an orb in the staff pose: the short staff
};

constexpr const char* Codes[7] = {"AM", "AI", "BA", "DZ", "NE", "PA", "SO"};  // monstats Code, ClassNames order

inline const char* NearestOf(int cls, const char* tok) noexcept {
	for (const Near& n : Nearest) {
		if (std::strcmp(n.cls, Codes[cls]) == 0 && std::strcmp(n.from, tok) == 0) {
			return n.to;
		}
	}
	return tok;
}

inline Result Dress(int cls, const uint32_t* codes, int n, uint8_t* parts) noexcept {
	Result r;
	if (cls < 0 || cls >= 7 || parts == nullptr) {
		return r;
	}
	const Lists& l = Classes[cls];
	const char* list[PartBytes] = {};
	list[HD] = l.hd, list[RH] = l.rh, list[LH] = l.lh, list[SH] = l.sh, list[S3] = l.s3;
	constexpr Part Empty[] = {RH, LH, SH};
	for (const Part p : Empty) {
		const int nil = IndexOf(list[p], "nil");
		if (nil >= 0) {
			parts[p] = static_cast<uint8_t>(nil);
		}
	}
	const auto set = [&](Part p, const char* tok) noexcept {
		tok = NearestOf(cls, tok);
		const int i = list[p] != nullptr ? IndexOf(list[p], tok) : -1;
		if (i >= 0) {
			parts[p] = static_cast<uint8_t>(i);
		}
		(i >= 0 ? r.shown : r.unshown) += 1;
	};
	int weapons = 0;
	for (int k = 0; k < n; ++k) {
		const Item* it = Find(codes[k]);
		if (it == nullptr) {
			continue;
		}
		switch (it->slot) {
		case Slot::Head:
			set(HD, it->tok);
			break;
		case Slot::Body:
			for (int p = 0; p < 6; ++p) {
				parts[p < 4 ? TR + p : S1 + (p - 4)] = it->body[p];
			}
			r.shown += 1;
			break;
		case Slot::Weapon: {
			const Part other = l.hand == RH ? LH : RH;
			// the first weapon: its class's hand, or the other hand when only that list has it (the Amazon's spear);
			// a second weapon: the left hand
			const Part p = weapons++ > 0 ? LH : IndexOf(list[l.hand], it->tok) < 0 && IndexOf(list[other], it->tok) >= 0 ? other : l.hand;
			set(p, it->tok);
			break;
		}
		case Slot::Shield:
			set(SH, it->tok);
			break;
		case Slot::Skull:
			set(S3, it->tok);
			break;
		}
	}
	return r;
}

}  // namespace pd2::invader_dress
