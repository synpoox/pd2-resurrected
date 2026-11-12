// Cartographer layout graph generator. See layout.h for the contract. Pure integer code.
#include "layout.h"

#include <algorithm>
#include <cstddef>

#include "rng.h"

namespace cartographer::layout {

namespace {

// Directions, in the order of the mask bits: W, E, S, N.
constexpr int kDx[4] = {-1, 1, 0, 0};
constexpr int kDy[4] = {0, 0, 1, -1};
constexpr uint8_t kBit[4] = {kWest, kEast, kSouth, kNorth};
constexpr int kDirW = 0, kDirE = 1, kDirS = 2, kDirN = 3;

Point step(Point p, int d) { return Point{p.x + kDx[d], p.y + kDy[d]}; }
int opposite(int d) { return d ^ 1; }
int manhattan(Point a, Point b) { return (a.x > b.x ? a.x - b.x : b.x - a.x) + (a.y > b.y ? a.y - b.y : b.y - a.y); }

// Direction index from a to the adjacent cell b, or -1.
int dirBetween(Point a, Point b)
{
	for (int d = 0; d < 4; ++d)
		if (step(a, d) == b) return d;
	return -1;
}

int popcount4(uint8_t m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1); }

// The working grid of one try: which cells are used and how they are linked.
class Grid {
public:
	Grid(int w, int h) : w_(w), h_(h), used_(static_cast<size_t>(w) * h, 0), mask_(static_cast<size_t>(w) * h, 0) {}

	int width() const { return w_; }
	int height() const { return h_; }
	bool inside(Point p) const { return p.x >= 0 && p.y >= 0 && p.x < w_ && p.y < h_; }
	bool used(Point p) const { return inside(p) && used_[idx(p)] != 0; }
	uint8_t mask(Point p) const { return mask_[idx(p)]; }
	int degree(Point p) const { return popcount4(mask_[idx(p)]); }

	// A new cell at c, hanging off `parent`, may touch no used cell except `parent` and `allowed` (side contact rule).
	bool canPlace(Point c, Point parent, Point allowed) const
	{
		if (!inside(c) || used(c)) return false;
		for (int d = 0; d < 4; ++d) {
			const Point n = step(c, d);
			if (used(n) && n != parent && n != allowed) return false;
		}
		return true;
	}

	void place(Point c) { used_[idx(c)] = 1; }

	void link(Point a, Point b)
	{
		const int d = dirBetween(a, b);
		mask_[idx(a)] = static_cast<uint8_t>(mask_[idx(a)] | kBit[d]);
		mask_[idx(b)] = static_cast<uint8_t>(mask_[idx(b)] | kBit[opposite(d)]);
	}

	// Removes a cell and every link to it.
	void remove(Point c)
	{
		for (int d = 0; d < 4; ++d) {
			const Point n = step(c, d);
			if (used(n)) mask_[idx(n)] = static_cast<uint8_t>(mask_[idx(n)] & ~kBit[opposite(d)]);
		}
		mask_[idx(c)] = 0;
		used_[idx(c)] = 0;
	}

private:
	size_t idx(Point p) const { return static_cast<size_t>(p.y) * w_ + p.x; }
	int w_, h_;
	std::vector<uint8_t> used_;
	std::vector<uint8_t> mask_;
};

// Everything a try builds, before it becomes a Layout.
struct Draft {
	std::vector<Point> path;
	std::vector<Branch> branches;
	std::vector<Branch> loops;
	bool ring = false;
	int eventIdx = -1;
	int bossIdx = -1;
};

void reserveRooms(const Rules &r, Draft &d)
{
	const int n = static_cast<int>(d.path.size());
	d.eventIdx = -1;
	d.bossIdx = -1;
	if (n < 3) return;
	if (d.ring) {
		if (r.bossRoom) d.bossIdx = n / 2;
		int e = n * r.eventPct / 200; // a ring has two halves: the event room is partway round the first one
		if (e < 1) e = 1;
		if (e == d.bossIdx) e = d.bossIdx - 1;
		d.eventIdx = e;
	} else {
		if (r.bossRoom) d.bossIdx = n - 1;
		int e = (n - 1) * r.eventPct / 100;
		if (e < 1) e = 1;
		if (e > n - 2) e = n - 2;
		d.eventIdx = e;
	}
}

bool isReserved(const Draft &d, Point p)
{
	if (d.path.empty()) return false;
	if (p == d.path[0]) return true;
	if (d.eventIdx >= 0 && p == d.path[static_cast<size_t>(d.eventIdx)]) return true;
	if (d.bossIdx >= 0 && p == d.path[static_cast<size_t>(d.bossIdx)]) return true;
	return false;
}

// Every used cell that a side branch may hang from, in row-major order.
std::vector<Point> eligibleRoots(const Grid &g, const Draft &d)
{
	std::vector<Point> out;
	for (int y = 0; y < g.height(); ++y)
		for (int x = 0; x < g.width(); ++x) {
			const Point p{x, y};
			if (g.used(p) && !isReserved(d, p) && g.degree(p) < 4) out.push_back(p);
		}
	return out;
}

// --- main path as a walk ------------------------------------------------------------------------------------------

bool walkMain(Grid &g, Pcg32 &rng, const Rules &r, Draft &d)
{
	const int w = g.width(), h = g.height();
	const int len = rng.range(r.pathMin, std::min(r.pathMax, w * h));

	// The entrance is on the border of the grid, looking inward along the roomier directions.
	std::vector<Point> border;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			if (x == 0 || y == 0 || x == w - 1 || y == h - 1) border.push_back(Point{x, y});
	const Point start = border[rng.below(static_cast<uint32_t>(border.size()))];

	int room[4];
	uint32_t total = 0;
	for (int dir = 0; dir < 4; ++dir) {
		int n = 0;
		for (Point p = step(start, dir); g.inside(p); p = step(p, dir)) ++n;
		room[dir] = n;
		total += static_cast<uint32_t>(n);
	}
	uint32_t pick = rng.below(total);
	int dir = 0;
	for (; dir < 4; ++dir) {
		if (pick < static_cast<uint32_t>(room[dir])) break;
		pick -= static_cast<uint32_t>(room[dir]);
	}

	g.place(start);
	d.path.push_back(start);
	Point cur = start;
	int turns = 0;
	for (int i = 1; i < len; ++i) {
		int next = dir;
		if (i > 1) {
			const int stepsLeft = len - i; // steps from here on that can still turn
			const bool mustTurn = r.turnsMin - turns >= stepsLeft;
			const bool mustStraight = turns >= r.turnsMax;
			const bool straightOk = g.canPlace(step(cur, dir), cur, Point{-9, -9});
			int turnDirs[2];
			int turnCount = 0;
			for (int t = 0; t < 4; ++t) {
				if (t == dir || t == opposite(dir)) continue;
				if (g.canPlace(step(cur, t), cur, Point{-9, -9})) turnDirs[turnCount++] = t;
			}
			if (mustTurn) {
				if (turnCount == 0) return false;
				next = turnDirs[rng.below(static_cast<uint32_t>(turnCount))];
			} else if (mustStraight) {
				if (!straightOk) return false;
			} else if (straightOk && (turnCount == 0 || rng.percent(r.straightPct))) {
				// straight
			} else if (turnCount > 0) {
				next = turnDirs[rng.below(static_cast<uint32_t>(turnCount))];
			} else {
				return false;
			}
			if (next != dir) ++turns;
		}
		const Point n = step(cur, next);
		if (!g.canPlace(n, cur, Point{-9, -9})) return false;
		g.place(n);
		g.link(cur, n);
		d.path.push_back(n);
		cur = n;
		dir = next;
	}
	return true;
}

// --- main path as a ring ------------------------------------------------------------------------------------------

bool ringValid(const std::vector<Point> &ring, int w, int h, std::vector<int> &scratch)
{
	const int n = static_cast<int>(ring.size());
	if (n < 4) return false;
	scratch.assign(static_cast<size_t>(w) * h, -1);
	for (int i = 0; i < n; ++i) {
		const Point p = ring[static_cast<size_t>(i)];
		if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return false;
		int &slot = scratch[static_cast<size_t>(p.y) * w + p.x];
		if (slot != -1) return false;
		slot = i;
	}
	for (int i = 0; i < n; ++i) {
		const Point p = ring[static_cast<size_t>(i)];
		if (manhattan(p, ring[static_cast<size_t>((i + 1) % n)]) != 1) return false;
		for (int dir = 0; dir < 4; ++dir) {
			const Point q = step(p, dir);
			if (q.x < 0 || q.y < 0 || q.x >= w || q.y >= h) continue;
			const int j = scratch[static_cast<size_t>(q.y) * w + q.x];
			if (j < 0) continue;
			const int diff = (j - i + n) % n;
			if (diff != 1 && diff != n - 1) return false; // side contact between cells that are not ring neighbours
		}
	}
	return true;
}

std::vector<Point> rectRing(int ox, int oy, int w, int h)
{
	std::vector<Point> ring;
	for (int x = 0; x < w; ++x) ring.push_back(Point{ox + x, oy});
	for (int y = 1; y < h; ++y) ring.push_back(Point{ox + w - 1, oy + y});
	for (int x = w - 2; x >= 0; --x) ring.push_back(Point{ox + x, oy + h - 1});
	for (int y = h - 2; y >= 1; --y) ring.push_back(Point{ox, oy + y});
	return ring;
}

// Pushes the segment ring[i] -> ring[i+1] outward by one cell (the ring grows by two cells).
std::vector<Point> bumpRing(const std::vector<Point> &ring, size_t i, int side)
{
	const size_t n = ring.size();
	const Point p = ring[i], q = ring[(i + 1) % n];
	const int dx = q.x - p.x, dy = q.y - p.y;
	const int nx = side ? -dy : dy, ny = side ? dx : -dx;
	std::vector<Point> out(ring);
	const Point a{p.x + nx, p.y + ny}, b{q.x + nx, q.y + ny};
	out.insert(out.begin() + static_cast<std::ptrdiff_t>(i + 1), {a, b});
	return out;
}

// Moves a corner cell to the opposite corner of its unit square (the ring keeps its length).
bool flipRing(std::vector<Point> &ring, size_t i)
{
	const size_t n = ring.size();
	const Point a = ring[(i + n - 1) % n], b = ring[(i + 1) % n], c = ring[i];
	if (a.x == b.x || a.y == b.y) return false;
	ring[i] = Point{a.x + b.x - c.x, a.y + b.y - c.y};
	return true;
}

void commitPath(Grid &g, Draft &d, const std::vector<Point> &path, bool ring)
{
	d.path = path;
	d.ring = ring;
	for (const Point &p : path) g.place(p);
	for (size_t i = 0; i + 1 < path.size(); ++i) g.link(path[i], path[i + 1]);
	if (ring) g.link(path.back(), path.front());
}

bool buildRing(Grid &g, Pcg32 &rng, const Rules &r, Draft &d)
{
	const int w = g.width(), h = g.height();
	int len = rng.range(r.pathMin, r.pathMax);
	if (len & 1) len = (len + 1 <= r.pathMax) ? len + 1 : len - 1;
	if (len < 4) return false;

	int rw = 0, rh = 0;
	for (int tries = 0; tries < 16; ++tries) {
		const int cw = rng.range(std::min(3, w), w), ch = rng.range(std::min(3, h), h);
		if (2 * (cw + ch) - 4 <= len) { rw = cw; rh = ch; break; }
	}
	if (rw == 0) { rw = std::min(3, w); rh = std::min(3, h); }
	if (2 * (rw + rh) - 4 > len) return false;

	std::vector<Point> ring = rectRing(rng.range(0, w - rw), rng.range(0, h - rh), rw, rh);
	std::vector<int> scratch;
	for (int tries = 0; static_cast<int>(ring.size()) < len && tries < 400; ++tries) {
		const size_t i = rng.below(static_cast<uint32_t>(ring.size()));
		std::vector<Point> trial = bumpRing(ring, i, static_cast<int>(rng.below(2)));
		if (ringValid(trial, w, h, scratch)) ring.swap(trial);
	}
	if (static_cast<int>(ring.size()) != len) return false;

	const int flips = rng.range(0, 24);
	for (int t = 0; t < flips; ++t) {
		const size_t i = rng.below(static_cast<uint32_t>(ring.size()));
		std::vector<Point> trial(ring);
		if (flipRing(trial, i) && ringValid(trial, w, h, scratch)) ring.swap(trial);
	}

	// Start anywhere on the ring, walking either way round.
	const size_t n = ring.size();
	const size_t s = rng.below(static_cast<uint32_t>(n));
	const bool reverse = rng.below(2) != 0;
	std::vector<Point> path;
	for (size_t k = 0; k < n; ++k) path.push_back(reverse ? ring[(s + n - k) % n] : ring[(s + k) % n]);
	commitPath(g, d, path, true);
	return true;
}

// --- side branches and extra loops --------------------------------------------------------------------------------

const Point kNoPoint{-9, -9};

// Grows a dead-end branch from `root`. Returns the cells placed (they are in the grid and linked).
std::vector<Point> growBranch(Grid &g, Pcg32 &rng, Point root, int len)
{
	std::vector<Point> cells;
	Point cur = root;
	int prev = -1;
	for (int k = 0; k < len; ++k) {
		int legal[4];
		int count = 0;
		for (int dir = 0; dir < 4; ++dir)
			if (g.canPlace(step(cur, dir), cur, kNoPoint)) legal[count++] = dir;
		if (count == 0) break;
		int pick = -1;
		if (prev >= 0 && rng.percent(60))
			for (int i = 0; i < count; ++i)
				if (legal[i] == prev) pick = prev;
		if (pick < 0) pick = legal[rng.below(static_cast<uint32_t>(count))];
		const Point n = step(cur, pick);
		g.place(n);
		g.link(cur, n);
		cells.push_back(n);
		cur = n;
		prev = pick;
	}
	return cells;
}

void rollback(Grid &g, const std::vector<Point> &cells)
{
	for (size_t i = cells.size(); i-- > 0;) g.remove(cells[i]);
}

bool addBranches(Grid &g, Pcg32 &rng, const Rules &r, Draft &d)
{
	const int want = rng.range(r.branchesMin, r.branchesMax);
	for (int b = 0; b < want; ++b) {
		for (int tries = 0; tries < 30; ++tries) {
			const std::vector<Point> roots = eligibleRoots(g, d);
			if (roots.empty()) break;
			const Point root = roots[rng.below(static_cast<uint32_t>(roots.size()))];
			const int len = rng.range(r.branchLenMin, r.branchLenMax);
			std::vector<Point> cells = growBranch(g, rng, root, len);
			if (static_cast<int>(cells.size()) >= r.branchLenMin) {
				Branch br;
				br.root = root;
				br.cells = std::move(cells);
				d.branches.push_back(std::move(br));
				break;
			}
			rollback(g, cells);
		}
	}
	return static_cast<int>(d.branches.size()) >= r.branchesMin;
}

// One extra loop: a branch that walks toward a target cell and links into it.
bool growLoop(Grid &g, Pcg32 &rng, Draft &d)
{
	const std::vector<Point> roots = eligibleRoots(g, d);
	if (roots.size() < 2) return false;
	const Point root = roots[rng.below(static_cast<uint32_t>(roots.size()))];
	const Point target = roots[rng.below(static_cast<uint32_t>(roots.size()))];
	if (target == root) return false;

	std::vector<Point> cells;
	Point cur = root;
	for (;;) {
		int legal[4];
		int count = 0;
		for (int dir = 0; dir < 4; ++dir) {
			const Point n = step(cur, dir);
			if (!g.canPlace(n, cur, target)) continue;
			if (cells.empty() && manhattan(n, target) == 1) continue; // a loop needs at least two new cells
			legal[count++] = dir;
		}
		if (count == 0 || static_cast<int>(cells.size()) >= kLoopBranchMax) break;
		int pick = legal[0];
		if (rng.percent(75)) {
			for (int i = 1; i < count; ++i)
				if (manhattan(step(cur, legal[i]), target) < manhattan(step(cur, pick), target)) pick = legal[i];
		} else {
			pick = legal[rng.below(static_cast<uint32_t>(count))];
		}
		const Point n = step(cur, pick);
		g.place(n);
		g.link(cur, n);
		cells.push_back(n);
		cur = n;
		if (manhattan(cur, target) == 1) {
			g.link(cur, target);
			Branch br;
			br.root = root;
			br.cells = std::move(cells);
			br.closes = true;
			br.closeTo = target;
			d.loops.push_back(std::move(br));
			return true;
		}
	}
	rollback(g, cells);
	return false;
}

bool addLoops(Grid &g, Pcg32 &rng, const Rules &r, Draft &d)
{
	const int want = rng.range(r.loopsMin, r.loopsMax);
	for (int l = 0; l < want; ++l)
		for (int tries = 0; tries < 40; ++tries)
			if (growLoop(g, rng, d)) break;
	return static_cast<int>(d.loops.size()) >= r.loopsMin;
}

// --- assembling a layout ------------------------------------------------------------------------------------------

Layout assemble(const Grid &g, const Draft &d)
{
	Layout l;
	l.width = g.width();
	l.height = g.height();
	l.cells.assign(static_cast<size_t>(l.width) * l.height, Cell{});
	for (int y = 0; y < l.height; ++y)
		for (int x = 0; x < l.width; ++x)
			if (g.used(Point{x, y})) l.at(x, y).mask = g.mask(Point{x, y});
	for (size_t i = 0; i < d.path.size(); ++i) {
		const Point p = d.path[i];
		Role role = Role::Path;
		if (i == 0) role = Role::Entrance;
		else if (static_cast<int>(i) == d.eventIdx) role = Role::Event;
		else if (static_cast<int>(i) == d.bossIdx) role = Role::Boss;
		l.at(p.x, p.y).role = role;
	}
	for (const Branch &b : d.branches)
		for (const Point &p : b.cells) l.at(p.x, p.y).role = Role::Branch;
	for (const Branch &b : d.loops)
		for (const Point &p : b.cells) l.at(p.x, p.y).role = Role::Branch;
	l.mainPath = d.path;
	l.eventIdx = d.eventIdx;
	l.bossIdx = d.bossIdx;
	l.ring = d.ring;
	l.branches = d.branches;
	l.loops = d.loops;
	return l;
}

// --- the fallback -------------------------------------------------------------------------------------------------

// Smallest run of rows between two rows of a serpentine so that side rooms of branchLenMin cells fit between them.
int rowGap(const Rules &r) { return r.branchesMin > 0 ? std::max(2, r.branchLenMin + 2) : 2; }

bool fallbackRing(Grid &g, const Rules &r, Draft &d)
{
	const int w = g.width(), h = g.height();
	int len = r.pathMin + (r.pathMin & 1);
	if (len > r.pathMax) return false;
	int rw = std::min(3, w), rh = std::min(3, h);
	for (;;) {
		if (rw < w && 2 * (rw + 1 + rh) - 4 <= len) ++rw;
		else if (rh < h && 2 * (rw + rh + 1) - 4 <= len) ++rh;
		else break;
	}
	std::vector<Point> ring = rectRing(0, 0, rw, rh);
	std::vector<int> scratch;
	while (static_cast<int>(ring.size()) < len) {
		bool found = false;
		for (size_t i = 0; i < ring.size() && !found; ++i)
			for (int side = 0; side < 2 && !found; ++side) {
				std::vector<Point> trial = bumpRing(ring, i, side);
				if (ringValid(trial, w, h, scratch)) { ring.swap(trial); found = true; }
			}
		if (!found) break;
	}
	if (!ringValid(ring, w, h, scratch)) return false;
	commitPath(g, d, ring, true);
	return true;
}

// A staircase until there are enough turns, then a serpentine of rows that fills the grid top to bottom.
void fallbackWalk(Grid &g, const Rules &r, Draft &d)
{
	const int len = std::min(r.pathMin, g.width() * g.height());
	Point cur{0, 0};
	g.place(cur);
	d.path.push_back(cur);
	int last = -1, turns = 0;
	auto advance = [&](int dir) {
		const Point n = step(cur, dir);
		if (!g.canPlace(n, cur, kNoPoint)) return false;
		g.place(n);
		g.link(cur, n);
		d.path.push_back(n);
		if (last >= 0 && dir != last) ++turns;
		last = dir;
		cur = n;
		return true;
	};
	for (int alt = 0; turns < r.turnsMin && static_cast<int>(d.path.size()) < len; alt ^= 1)
		if (!advance(alt ? kDirS : kDirE)) break;
	int heading = kDirE;
	const int gap = rowGap(r);
	while (static_cast<int>(d.path.size()) < len) {
		if (advance(heading)) continue;
		bool moved = true;
		for (int k = 0; k < gap && moved && static_cast<int>(d.path.size()) < len; ++k) moved = advance(kDirS);
		if (!moved) break;
		heading = opposite(heading);
	}
}

void fallbackBranches(Grid &g, const Rules &r, Draft &d)
{
	const std::vector<int> order = {kDirS, kDirN, kDirE, kDirW};
	std::vector<Point> roots = d.path; // main path cells first, in order
	for (size_t i = 0; i < roots.size() && static_cast<int>(d.branches.size()) < r.branchesMin; ++i) {
		const Point root = roots[i];
		if (isReserved(d, root)) continue;
		for (size_t o = 0; o < order.size() && static_cast<int>(d.branches.size()) < r.branchesMin; ++o) {
			std::vector<Point> cells;
			Point cur = root;
			bool ok = true;
			for (int k = 0; k < r.branchLenMin && ok; ++k) {
				const Point n = step(cur, order[o]);
				if (!g.canPlace(n, cur, kNoPoint)) { ok = false; break; }
				g.place(n);
				g.link(cur, n);
				cells.push_back(n);
				cur = n;
			}
			if (ok) {
				Branch br;
				br.root = root;
				br.cells = std::move(cells);
				d.branches.push_back(std::move(br));
			} else {
				rollback(g, cells);
			}
		}
	}
}

} // namespace

// --- Layout ---------------------------------------------------------------------------------------------------------

int Layout::cellCount() const
{
	int n = 0;
	for (const Cell &c : cells) n += c.role != Role::Empty ? 1 : 0;
	return n;
}

int Layout::turns() const
{
	int t = 0;
	for (size_t i = 2; i < mainPath.size(); ++i) {
		const int a = dirBetween(mainPath[i - 2], mainPath[i - 1]);
		const int b = dirBetween(mainPath[i - 1], mainPath[i]);
		if (a != b) ++t;
	}
	return t;
}

// --- names and presets ---------------------------------------------------------------------------------------------

namespace {

bool equalsNoCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
		if (x != y) return false;
	}
	return true;
}

struct Preset {
	int pathMin, pathMax, straight, turnsMin, turnsMax, branchesMin, branchesMax, lenMin, lenMax, loopsMin, loopsMax;
	bool loop;
};

// [shape][size]: Small, Medium, Large. The grid is 6x6, 9x9 and 12x12 macro-cells.
const Preset kPresets[kShapeCount][kSizeCount] = {
	// Corridor: one long, mostly straight path, few dead ends.
	{{8, 10, 90, 0, 2, 0, 1, 1, 2, 0, 0, false},
	 {14, 18, 90, 0, 3, 0, 1, 1, 2, 0, 0, false},
	 {22, 28, 90, 0, 4, 0, 1, 1, 2, 0, 0, false}},
	// Winding: one long path with many turns, few dead ends.
	{{10, 14, 40, 5, 99, 0, 1, 1, 2, 0, 0, false},
	 {18, 24, 40, 8, 99, 0, 1, 1, 2, 0, 0, false},
	 {28, 38, 40, 12, 99, 0, 1, 1, 2, 0, 0, false}},
	// Branching: a main path with several side rooms and dead ends.
	{{8, 10, 70, 1, 99, 2, 3, 1, 3, 0, 0, false},
	 {12, 16, 70, 1, 99, 3, 5, 2, 4, 0, 0, false},
	 {18, 24, 70, 1, 99, 4, 7, 2, 5, 0, 0, false}},
	// Loop: the path runs in a ring and returns to its start, with a few short side rooms.
	{{8, 12, 0, 3, 99, 0, 2, 1, 2, 0, 0, true},
	 {12, 20, 0, 3, 99, 0, 2, 1, 2, 0, 0, true},
	 {20, 32, 0, 3, 99, 0, 3, 1, 2, 0, 0, true}},
	// Sprawl: wide and open, with loops and many branches.
	{{12, 16, 0, 3, 99, 2, 4, 1, 2, 0, 1, true},
	 {20, 28, 0, 3, 99, 4, 7, 1, 3, 0, 2, true},
	 {32, 40, 0, 3, 99, 6, 10, 2, 4, 0, 2, true}},
};

const int kGridSize[kSizeCount] = {6, 9, 12};

} // namespace

const char *shapeName(Shape shape)
{
	static const char *const names[kShapeCount] = {"Corridor", "Winding", "Branching", "Loop", "Sprawl"};
	return names[static_cast<int>(shape)];
}

const char *sizeName(Size size)
{
	static const char *const names[kSizeCount] = {"Small", "Medium", "Large"};
	return names[static_cast<int>(size)];
}

std::optional<Shape> parseShapeName(std::string_view name)
{
	for (int i = 0; i < kShapeCount; ++i)
		if (equalsNoCase(name, shapeName(static_cast<Shape>(i)))) return static_cast<Shape>(i);
	return std::nullopt;
}

std::optional<Size> parseSizeName(std::string_view name)
{
	for (int i = 0; i < kSizeCount; ++i)
		if (equalsNoCase(name, sizeName(static_cast<Size>(i)))) return static_cast<Size>(i);
	return std::nullopt;
}

Rules presetRules(Shape shape, Size size)
{
	const Preset &p = kPresets[static_cast<int>(shape)][static_cast<int>(size)];
	Rules r;
	r.width = r.height = kGridSize[static_cast<int>(size)];
	r.pathMin = p.pathMin;
	r.pathMax = p.pathMax;
	r.straightPct = p.straight;
	r.turnsMin = p.turnsMin;
	r.turnsMax = p.turnsMax;
	r.branchesMin = p.branchesMin;
	r.branchesMax = p.branchesMax;
	r.branchLenMin = p.lenMin;
	r.branchLenMax = p.lenMax;
	r.loopsMin = p.loopsMin;
	r.loopsMax = p.loopsMax;
	r.loop = p.loop;
	return r;
}

std::string validateRules(const Rules &r)
{
	if (r.width < 2 || r.height < 2 || r.width > 32 || r.height > 32)
		return "Width and Height must each be between 2 and 32 cells.";
	if (r.pathMin < 4) return "PathMin must be at least 4: the path needs an entrance, an event room, a boss room and one more.";
	if (r.pathMin > r.pathMax) return "PathMin can't be more than PathMax.";
	if (r.pathMax > r.width * r.height) return "PathMax is more cells than fit in the Width x Height grid.";
	if (r.straightPct < 0 || r.straightPct > 100) return "Straight must be a percent between 0 and 100.";
	if (r.turnsMin < 0) return "MinTurns can't be negative.";
	if (r.turnsMin > r.turnsMax) return "MinTurns can't be more than MaxTurns.";
	if (r.branchesMin < 0 || r.branchesMin > r.branchesMax) return "MinBranches can't be negative or more than MaxBranches.";
	if (r.branchLenMin < 1 || r.branchLenMin > r.branchLenMax) return "MinBranchLen must be at least 1 and no more than MaxBranchLen.";
	if (r.loopsMin < 0 || r.loopsMin > r.loopsMax) return "MinLoops can't be negative or more than MaxLoops.";
	if (r.eventPct < 1 || r.eventPct > 99) return "The event room position must be between 1 and 99 percent.";
	if (r.loop) {
		bool evenFits = false;
		for (int n = r.pathMin; n <= r.pathMax; ++n)
			if ((n & 1) == 0) evenFits = true;
		if (!evenFits) return "A Loop path needs an even number of cells: PathMin to PathMax holds none.";
		if (r.width < 3 || r.height < 3) return "A Loop needs a grid at least 3 cells wide and 3 high.";
	}
	return std::string();
}

// --- generation ----------------------------------------------------------------------------------------------------

std::optional<Layout> generateAttempt(const Rules &r, uint64_t attemptSeed)
{
	Pcg32 rng(attemptSeed);
	Grid g(r.width, r.height);
	Draft d;
	if (r.loop) {
		if (!buildRing(g, rng, r, d)) return std::nullopt;
	} else {
		if (!walkMain(g, rng, r, d)) return std::nullopt;
		d.ring = false;
	}
	reserveRooms(r, d);
	if (!addBranches(g, rng, r, d)) return std::nullopt;
	if (!addLoops(g, rng, r, d)) return std::nullopt;
	return assemble(g, d);
}

Layout generateFallback(const Rules &r, uint64_t seed)
{
	Grid g(r.width, r.height);
	Draft d;
	if (r.loop) {
		fallbackRing(g, r, d);
	} else {
		fallbackWalk(g, r, d);
	}
	reserveRooms(r, d);
	fallbackBranches(g, r, d);
	Layout l = assemble(g, d);
	l.seed = seed;
	l.attempt = kRetries;
	l.fallback = true;
	l.rulesMet = checkLayout(l, r).empty();
	return l;
}

Layout generate(const Rules &r, uint64_t seed)
{
	if (validateRules(r).empty()) {
		for (int k = 0; k < kRetries; ++k) {
			std::optional<Layout> l = generateAttempt(r, splitmix64(seed ^ static_cast<uint64_t>(k)));
			if (l && checkLayout(*l, r).empty()) {
				l->seed = seed;
				l->attempt = k;
				l->fallback = false;
				l->rulesMet = true;
				return std::move(*l);
			}
		}
	}
	return generateFallback(r, seed);
}

Layout generate(Shape shape, Size size, uint64_t seed) { return generate(presetRules(shape, size), seed); }

// --- checking ------------------------------------------------------------------------------------------------------

std::vector<std::string> checkLayout(const Layout &l, const Rules &r)
{
	std::vector<std::string> bad;
	auto fail = [&](std::string s) { bad.push_back(std::move(s)); };
	auto at = [](int x, int y) { return "(" + std::to_string(x) + "," + std::to_string(y) + ")"; };
	auto pt = [&](Point p) { return at(p.x, p.y); };

	if (l.width != r.width || l.height != r.height || l.cells.size() != static_cast<size_t>(r.width) * r.height) {
		fail("the grid is not the width and height of the rules");
		return bad;
	}
	auto used = [&](Point p) { return l.inside(p.x, p.y) && l.at(p.x, p.y).role != Role::Empty; };
	auto linked = [&](Point a, Point b) {
		const int d = dirBetween(a, b);
		return d >= 0 && used(a) && used(b) && (l.at(a.x, a.y).mask & kBit[d]) != 0 &&
		       (l.at(b.x, b.y).mask & kBit[opposite(d)]) != 0;
	};

	// Cells: masks, edges and side contact.
	int usedCount = 0, bitCount = 0;
	for (int y = 0; y < l.height; ++y)
		for (int x = 0; x < l.width; ++x) {
			const Cell &c = l.at(x, y);
			const Point p{x, y};
			if (c.role == Role::Empty) {
				if (c.mask != 0) fail("empty cell " + at(x, y) + " has exits");
				continue;
			}
			++usedCount;
			if (c.mask == 0) fail("cell " + at(x, y) + " has mask 0");
			if (c.mask & ~0x0F) fail("cell " + at(x, y) + " has a bad mask");
			bitCount += popcount4(c.mask & 0x0F);
			for (int d = 0; d < 4; ++d) {
				const Point n = step(p, d);
				const bool bit = (c.mask & kBit[d]) != 0;
				if (bit && !used(n)) fail("cell " + at(x, y) + " has an exit to nothing");
				else if (bit && !(l.at(n.x, n.y).mask & kBit[opposite(d)])) fail("cell " + at(x, y) + " and its neighbour disagree on a facing bit");
				else if (!bit && used(n)) fail("cells " + at(x, y) + " and " + pt(n) + " touch without a link");
			}
		}
	if (usedCount == 0) { fail("the layout is empty"); return bad; }

	// Connectivity.
	{
		std::vector<uint8_t> seen(l.cells.size(), 0);
		std::vector<Point> stack;
		for (int y = 0; y < l.height && stack.empty(); ++y)
			for (int x = 0; x < l.width && stack.empty(); ++x)
				if (l.at(x, y).role != Role::Empty) { stack.push_back(Point{x, y}); seen[static_cast<size_t>(y) * l.width + x] = 1; }
		int reached = 0;
		while (!stack.empty()) {
			const Point p = stack.back();
			stack.pop_back();
			++reached;
			for (int d = 0; d < 4; ++d) {
				const Point n = step(p, d);
				if (!(l.at(p.x, p.y).mask & kBit[d]) || !l.inside(n.x, n.y)) continue;
				uint8_t &s = seen[static_cast<size_t>(n.y) * l.width + n.x];
				if (!s) { s = 1; stack.push_back(n); }
			}
		}
		if (reached != usedCount) fail("the layout is not connected");
	}

	// Main path.
	const int n = static_cast<int>(l.mainPath.size());
	if (n < r.pathMin || n > r.pathMax) fail("the main path has " + std::to_string(n) + " cells, outside " + std::to_string(r.pathMin) + ".." + std::to_string(r.pathMax));
	if (l.ring != r.loop) fail(r.loop ? "the path is not a ring" : "the path is a ring");
	int accounted = 0;
	std::vector<uint8_t> claimed(l.cells.size(), 0);
	auto claim = [&](Point p) {
		if (!l.inside(p.x, p.y)) { fail("a room lies outside the bounds"); return; }
		uint8_t &c = claimed[static_cast<size_t>(p.y) * l.width + p.x];
		if (c) fail("cell " + pt(p) + " is listed twice");
		c = 1;
		++accounted;
	};
	for (int i = 0; i < n; ++i) {
		const Point p = l.mainPath[static_cast<size_t>(i)];
		claim(p);
		if (!used(p)) { fail("main path cell " + pt(p) + " is empty"); continue; }
		if (i > 0 && !linked(l.mainPath[static_cast<size_t>(i - 1)], p)) fail("main path cells " + std::to_string(i - 1) + " and " + std::to_string(i) + " are not linked");
	}
	if (n > 0) {
		if (used(l.mainPath[0]) && l.at(l.mainPath[0].x, l.mainPath[0].y).role != Role::Entrance) fail("the first path cell is not the entrance");
		if (l.ring && n > 1 && !linked(l.mainPath.back(), l.mainPath.front())) fail("the ring does not close onto the entrance");
	}
	if (l.turns() < r.turnsMin || l.turns() > r.turnsMax) fail("the main path has " + std::to_string(l.turns()) + " turns, outside " + std::to_string(r.turnsMin) + ".." + std::to_string(r.turnsMax));

	// Reserved rooms.
	if (n >= 3) {
		if (l.eventIdx < 1 || l.eventIdx >= n) {
			fail("the event room is not on the main path");
		} else {
			const Point e = l.mainPath[static_cast<size_t>(l.eventIdx)];
			if (!l.ring && l.eventIdx >= n - 1) fail("the event room is at the end of the path");
			if (used(e) && l.at(e.x, e.y).role != Role::Event) fail("the event room cell has the wrong role");
			if (used(e) && popcount4(l.at(e.x, e.y).mask) != 2) fail("the event room is not partway along (it has a branch)");
		}
		if (r.bossRoom) {
			if (l.bossIdx < 1 || l.bossIdx >= n || l.bossIdx == l.eventIdx) {
				fail("there is no boss room");
			} else {
				const Point b = l.mainPath[static_cast<size_t>(l.bossIdx)];
				if (!l.ring && l.bossIdx != n - 1) fail("the boss room is not at the end of the path");
				if (used(b) && l.at(b.x, b.y).role != Role::Boss) fail("the boss room cell has the wrong role");
				if (used(b) && popcount4(l.at(b.x, b.y).mask) != (l.ring ? 2 : 1)) fail("the boss room has the wrong number of exits");
			}
		} else if (l.bossIdx != -1) {
			fail("a boss room was reserved but the rules have none");
		}
	}
	auto reserved = [&](Point p) {
		return (n > 0 && p == l.mainPath[0]) || (l.eventIdx >= 0 && l.eventIdx < n && p == l.mainPath[static_cast<size_t>(l.eventIdx)]) ||
		       (l.bossIdx >= 0 && l.bossIdx < n && p == l.mainPath[static_cast<size_t>(l.bossIdx)]);
	};

	// Side branches and loops.
	auto checkChain = [&](const Branch &b, const char *what) {
		if (!used(b.root) || reserved(b.root)) fail(std::string(what) + " hangs from a missing or reserved room");
		Point prev = b.root;
		for (const Point &p : b.cells) {
			claim(p);
			if (used(p) && l.at(p.x, p.y).role != Role::Branch) fail(std::string(what) + " cell " + pt(p) + " has the wrong role");
			if (!linked(prev, p)) fail(std::string(what) + " cell " + pt(p) + " is not linked to the cell before it");
			prev = p;
		}
	};
	if (static_cast<int>(l.branches.size()) < r.branchesMin || static_cast<int>(l.branches.size()) > r.branchesMax)
		fail("there are " + std::to_string(l.branches.size()) + " side rooms, outside " + std::to_string(r.branchesMin) + ".." + std::to_string(r.branchesMax));
	for (const Branch &b : l.branches) {
		checkChain(b, "a side room");
		const int len = static_cast<int>(b.cells.size());
		if (len < r.branchLenMin || len > r.branchLenMax) fail("a side room has " + std::to_string(len) + " cells, outside " + std::to_string(r.branchLenMin) + ".." + std::to_string(r.branchLenMax));
		if (len > 0 && used(b.cells.back()) && popcount4(l.at(b.cells.back().x, b.cells.back().y).mask) != 1) fail("a side room does not end in a dead end");
	}
	if (static_cast<int>(l.loops.size()) < r.loopsMin || static_cast<int>(l.loops.size()) > r.loopsMax)
		fail("there are " + std::to_string(l.loops.size()) + " extra loops, outside " + std::to_string(r.loopsMin) + ".." + std::to_string(r.loopsMax));
	for (const Branch &b : l.loops) {
		checkChain(b, "an extra loop");
		const int len = static_cast<int>(b.cells.size());
		if (len < 2 || len > kLoopBranchMax) fail("an extra loop has " + std::to_string(len) + " cells");
		if (!b.closes || len == 0 || !linked(b.cells.back(), b.closeTo) || reserved(b.closeTo)) fail("an extra loop does not rejoin the layout");
	}
	if (accounted != usedCount) fail("some cells belong to no path, side room or loop");

	// Graph shape: a tree plus one link per loop.
	const int expectLinks = usedCount - 1 + (l.ring ? 1 : 0) + static_cast<int>(l.loops.size());
	if (bitCount / 2 != expectLinks) fail("the number of links does not match the rooms and loops");
	return bad;
}

// --- hash and dump -------------------------------------------------------------------------------------------------

uint64_t layoutHash(const Layout &l)
{
	uint64_t h = 0xcbf29ce484222325ull;
	auto byte = [&](uint8_t b) { h = (h ^ b) * 0x100000001b3ull; };
	auto word = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>(v >> (8 * i))); };
	word(static_cast<uint32_t>(l.width));
	word(static_cast<uint32_t>(l.height));
	for (const Cell &c : l.cells) { byte(c.mask); byte(static_cast<uint8_t>(c.role)); }
	word(static_cast<uint32_t>(l.mainPath.size()));
	for (const Point &p : l.mainPath) { word(static_cast<uint32_t>(p.x)); word(static_cast<uint32_t>(p.y)); }
	word(static_cast<uint32_t>(l.eventIdx + 1));
	word(static_cast<uint32_t>(l.bossIdx + 1));
	byte(l.ring ? 1 : 0);
	return h;
}

std::string toAscii(const Layout &l)
{
	const int cw = 2 * l.width - 1, ch = 2 * l.height - 1;
	if (cw <= 0 || ch <= 0) return std::string();
	std::vector<std::string> rows(static_cast<size_t>(ch), std::string(static_cast<size_t>(cw), ' '));
	for (int y = 0; y < l.height; ++y)
		for (int x = 0; x < l.width; ++x) {
			const Cell &c = l.at(x, y);
			char glyph = '.';
			switch (c.role) {
			case Role::Entrance: glyph = '@'; break;
			case Role::Event: glyph = '!'; break;
			case Role::Boss: glyph = 'B'; break;
			case Role::Path: glyph = '#'; break;
			case Role::Branch: glyph = 'o'; break;
			case Role::Empty: break;
			}
			rows[static_cast<size_t>(2 * y)][static_cast<size_t>(2 * x)] = glyph;
			if (c.mask & kEast) rows[static_cast<size_t>(2 * y)][static_cast<size_t>(2 * x + 1)] = '-';
			if (c.mask & kSouth) rows[static_cast<size_t>(2 * y + 1)][static_cast<size_t>(2 * x)] = '|';
		}
	std::string out;
	for (std::string &row : rows) {
		while (!row.empty() && row.back() == ' ') row.pop_back();
		out += row;
		out += '\n';
	}
	return out;
}

} // namespace cartographer::layout
