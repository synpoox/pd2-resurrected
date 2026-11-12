// Cartographer layout graph generator (phase 2). Pure C++: no engine headers, no SDK, no floats, no <random>.
//
// A layout is a set of macro-cells on a W x H grid, joined by links. Both builders read the same output:
//   * the maze builder reads each cell's exit mask as a room's exits;
//   * the outdoor builder reads each cell's mask as the facing bits of a macro-cell.
// Mask bits: W=1, E=2, S=4, N=8 (the bits of the Act 3 jungle edge rule). Coordinates: x grows east, y grows south.
// Neighbouring cells always agree: if a cell has E set, the cell to its east has W set.
//
// Rules the generator enforces (see checkLayout, which tests them independently):
//   * one main path of pathMin..pathMax cells from the entrance, with turnsMin..turnsMax direction changes;
//   * the event room sits partway along the main path, and the boss room at its end (a Loop: opposite the entrance);
//   * side branches (dead-end rooms): branchesMin..branchesMax of them, each branchLenMin..branchLenMax cells long;
//   * a Loop closes the main path back onto the entrance, and "extra loops" are side branches that rejoin the layout;
//   * unlinked cells never touch side to side, and every cell lies inside the width x height bounds.
// Determinism: the layout depends on (rules, seed) only. Retries use splitmix64(seed ^ k) for k < 32, then a fallback.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cartographer::layout {

// Exit mask bits.
constexpr uint8_t kWest = 1;
constexpr uint8_t kEast = 2;
constexpr uint8_t kSouth = 4;
constexpr uint8_t kNorth = 8;

constexpr int kRetries = 32; // seed_k = splitmix64(seed ^ k) for k < kRetries, then the fallback

struct Point {
	int x = 0;
	int y = 0;
	bool operator==(const Point &o) const { return x == o.x && y == o.y; }
	bool operator!=(const Point &o) const { return !(*this == o); }
};

enum class Shape : uint8_t { Corridor, Winding, Branching, Loop, Sprawl };
enum class Size : uint8_t { Small, Medium, Large };
constexpr int kShapeCount = 5;
constexpr int kSizeCount = 3;

// What a cell is for. Entrance, Event and Boss are the reserved rooms.
enum class Role : uint8_t { Empty, Entrance, Path, Event, Boss, Branch };

// The numeric rules of a shape at a size. Every shape preset and every shapes.txt row resolves to one of these.
struct Rules {
	int width = 8;         // grid bounds in macro-cells
	int height = 8;
	int pathMin = 10;      // main path length in cells, entrance and boss room included
	int pathMax = 14;
	int straightPct = 70;  // chance to keep going straight at each step of the main path (a walk; not used by rings)
	int turnsMin = 0;      // direction changes along the main path
	int turnsMax = 99;
	int branchesMin = 0;   // dead-end side rooms
	int branchesMax = 0;
	int branchLenMin = 1;  // cells in one side room
	int branchLenMax = 2;
	bool loop = false;     // the main path is a ring: its last cell links back to the entrance
	int loopsMin = 0;      // extra loops: side branches that rejoin the layout
	int loopsMax = 0;
	bool bossRoom = true;  // reserve the boss room at the end of the path
	int eventPct = 50;     // the event room is this percent of the way along the main path
};

// Extra loops are internal-sized: a loop branch has 2..kLoopBranchMax cells.
constexpr int kLoopBranchMax = 10;

struct Cell {
	uint8_t mask = 0; // exit bits; 0 only for an empty cell
	Role role = Role::Empty;
};

struct Branch {
	Point root;               // the existing cell it hangs from
	std::vector<Point> cells; // in order from the root outwards
	bool closes = false;      // an extra loop: its last cell also links to closeTo
	Point closeTo;
};

struct Layout {
	int width = 0;
	int height = 0;
	std::vector<Cell> cells; // width * height, row-major (index = y * width + x)

	std::vector<Point> mainPath; // entrance first; for a Loop, the ring in walking order
	int eventIdx = -1;           // index into mainPath of the event room, or -1
	int bossIdx = -1;            // index into mainPath of the boss room, or -1
	bool ring = false;           // the last main path cell links back to the first
	std::vector<Branch> branches; // dead-end side rooms
	std::vector<Branch> loops;    // extra loops

	uint64_t seed = 0;       // the seed given to generate()
	int attempt = 0;         // k of the winning retry, or kRetries when the fallback was used
	bool fallback = false;   // the fallback layout was used
	bool rulesMet = false;   // checkLayout found no violation against the rules used

	const Cell &at(int x, int y) const { return cells[static_cast<size_t>(y) * width + x]; }
	Cell &at(int x, int y) { return cells[static_cast<size_t>(y) * width + x]; }
	bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
	int cellCount() const;       // used cells
	int turns() const;           // direction changes along the main path (the ring closing step is not counted)
};

// --- generation -------------------------------------------------------------------------------------------------

Rules presetRules(Shape shape, Size size);
std::optional<Shape> parseShapeName(std::string_view name); // case-insensitive
std::optional<Size> parseSizeName(std::string_view name);   // case-insensitive
const char *shapeName(Shape shape);
const char *sizeName(Size size);

// Plain-English problem with the rules themselves (empty when they are usable).
std::string validateRules(const Rules &rules);

// The layout for (rules, seed). Always returns a layout: after kRetries failed tries it returns the fallback, and
// rulesMet says whether even that met the rules (always true for the presets; a shapes.txt shape can be too tight).
Layout generate(const Rules &rules, uint64_t seed);
Layout generate(Shape shape, Size size, uint64_t seed);

// The deterministic fallback on its own (what generate() returns after the retries are spent).
Layout generateFallback(const Rules &rules, uint64_t seed);

// One try with an exact random seed, without checking it. Returns std::nullopt when the try got stuck.
std::optional<Layout> generateAttempt(const Rules &rules, uint64_t attemptSeed);

// --- checking and dumping ---------------------------------------------------------------------------------------

// Every rule the layout breaks, in plain English. Empty means the layout is valid. This does not use the generator.
std::vector<std::string> checkLayout(const Layout &layout, const Rules &rules);

uint64_t layoutHash(const Layout &layout); // FNV-1a over the grid, roles and main path: stable across platforms
std::string toAscii(const Layout &layout);  // '@' entrance, '!' event, 'B' boss, '#' path, 'o' side room, '-' '|' links

// --- shapes.txt (expert overrides) ------------------------------------------------------------------------------

// One row of shapes.txt. Blank cells inherit from the Base shape at the map's size.
struct ShapeDef {
	std::string name;
	std::string base; // a preset name or an earlier custom shape; blank means Winding
	std::optional<int> pathMin, pathMax, straightPct, turnsMin, turnsMax;
	std::optional<int> branchesMin, branchesMax, branchLenMin, branchLenMax;
	std::optional<bool> loop;
	std::optional<int> loopsMin, loopsMax, width, height;
	std::optional<bool> bossRoom;
};

struct ShapeTable {
	std::vector<ShapeDef> shapes;
};

struct ParseResult {
	ShapeTable table;
	std::vector<std::string> errors; // plain English, one per problem, with the line
	bool ok() const { return errors.empty(); }
};

// Parses the text of shapes.txt (tab-separated, header row). Checks every shape at every size, including that
// the generator can build it. `fileName` only appears in messages.
ParseResult parseShapesTxt(std::string_view text, std::string_view fileName = "shapes.txt");

// Rules for a shape name (a preset or a custom shape from the table) at a size. False with a plain-English error.
bool resolveRules(const ShapeTable &table, std::string_view shapeName, Size size, Rules &out, std::string &error);

} // namespace cartographer::layout
