# include <Siv3D.hpp>

/*
このプログラムはターン制戦略ゲームである。5つの駒(兵士・兵士・騎士・銃士・ヒーラー)を操作し、相手の駒(同じ)を全滅させたら勝ちのシンプルなルールとなっている。
操作方法等は以下の通りである。

1. プログラムを実行し、「Siv Tactics」の画面が現れたらクリックして開始する(文字が出てすぐ押すと反応しないが、5秒前後したら押すと反応する)

2. 開始すると早速自分の手駒(漢字青色)と相手の手駒(漢字赤色)と地形が表示される。地形と駒の配置(駒は敵味方を区別した位置)はランダムで、黒は障害物である。

3. 始めは毎回自分のターンである。移動したい駒にカーソルをあててクリックし、クリックすると現れる青のマスに一度だけ移動できる。
　 もし、そのままがいい時はもう一度その場でクリックする。移動完了した駒は灰色となる。また、意図してない駒をクリックしたときは右クリックでキャンセルできる。
   なお、赤色か赤斜線が入ったマスは攻撃範囲であり、攻撃範囲に相手の駒があると攻撃を行う。
   その駒の詳細なHPや攻撃力を知りたいときは駒にカーソルを当てると画面下に表示される。
   ※ ヒーラーは攻撃ではなく回復を担う。また、全ての駒は障害物や味方越しに攻撃できない。

4. 全ての手駒を操作完了したら相手のターンになる。

5. 3,4を繰り返し、相手の駒か自分の駒がなくなるまで遊ぶ。

6. 勝てば「WIN」負ければ「LOSE」と表示され、クリックすると初期画面に戻り、もう一度プレイできる。

7. ウィンドウを閉じるときは右上の×で閉じる。


※ 攻撃・回復の対象選択ルール

	1. 攻撃(または回復)範囲内に複数の対象がいる場合、最も距離(マンハッタン距離)が近い1個体を選択する。
	2. 距離が同じ対象が複数存在する場合、内部配列のインデックスが若い方を優先する。

	※ マンハッタン距離：格子状の道に沿って移動した長さ。X座標の差の絶対値とY座標の差の絶対値を足したもの。

※ 相手の行動について

	1. 索敵
	   相手の駒とのマンハッタン距離を計算し、最も近い個体を目標に設定する。
	2. 移動
	   目標に最も近づける隣接マスを選択して移動する。(1マス)

※ プログラム作成において生じたエラー修正や警告の削減と実装において新たに必要なものの探索と学習において生成AIを多用した。
　 加えて、コードを指定行数に抑えるために生成AIを利用しつつ調整した。(コメント含めず300行)

※ 一応WandboxのURLも添付
https://wandbox.org/permlink/Ushs6OhvdXRYEtDf#
*/

constexpr int32 CellSize = 60, Cols = 12, Rows = 10;

enum class GameState { PlayerSelect, PlayerMove, EnemyTurn };
enum class Job { Soldier, Knight, Gunner, Healer };

struct JobStat
{
	int32 hp, atk, mv, rng;
	String name;
};

const JobStat JobData[] =
{
	{ 100, 30, 3, 1, U"兵" }, { 80, 25, 4, 1, U"騎" },
	{ 60, 25, 2, 12, U"銃" }, { 50, -30, 3, 2, U"回" }
};

struct GameData { int32 turnCount = 1; String resultMessage = U""; };
using App = SceneManager<String, GameData>;

struct EffectAnim : IEffect
{
	Vec2 pos; ColorF color; String type;
	EffectAnim(const Vec2& p, const ColorF& c, String t) : pos(p), color(c), type(t) {}
	bool update(double t) override
	{
		double a = 1.0 - (t / 0.5);
		if (type == U"Heal") Shape2D::Plus(20, 5, pos.movedBy(0, -t * 30)).draw(color.withAlpha(a));
		else {
			Line(pos.movedBy(-15, -15), pos.movedBy(15, 15)).draw(4, color.withAlpha(a));
			Line(pos.movedBy(15, -15), pos.movedBy(-15, 15)).draw(4, color.withAlpha(a));
		}
		return t < 0.5;
	}
};

struct Unit
{
	Point pos; bool isPlayer, isActed = false;
	int32 jobIndex, hp;

	Unit(Point p, bool _isP, int32 _job) : pos(p), isPlayer(_isP), jobIndex(_job) {
		hp = JobData[jobIndex].hp;
	}

	void draw() const
	{
		Vec2 c = pos * CellSize + Vec2{ 30, 30 };
		ColorF col = isActed ? Palette::Gray : (isPlayer ? Palette::Dodgerblue : Palette::Red);
		FontAsset(U"F")(JobData[jobIndex].name).drawAt(c, col);
		RectF(c.x - 20, c.y - 25, 40, 4).draw(Palette::Black);
		RectF(c.x - 20, c.y - 25, 40 * ((double)hp / JobData[jobIndex].hp), 4).draw(Palette::Limegreen);
	}
};

class Title : public App::Scene
{
public:
	Title(const InitData& init) : IScene(init) {}
	void update() override { if (MouseL.down()) { getData().turnCount = 1; changeScene(U"Game"); } }
	void draw() const override {
		FontAsset(U"T")(U"Siv Tactics").drawAt(Scene::Center().movedBy(0, -50));
		FontAsset(U"M")(U"クリックで開始").drawAt(Scene::Center().movedBy(0, 50));
	}
};

class Game : public App::Scene
{
private:
	Grid<int32> map; Array<Unit> units;
	GameState state = GameState::PlayerSelect;
	int32 selIdx = -1; double enemyTimer = 0.0;
	Effect effect; Texture slashTexture;

	bool isBlocked(Point from, Point to) const
	{
		if (from == to) return false;
		Point dir = { 0, 0 };
		if (from.x == to.x) dir.y = (to.y > from.y) ? 1 : -1;
		else if (from.y == to.y) dir.x = (to.x > from.x) ? 1 : -1;
		else return false;

		for (Point p = from + dir; p != to; p += dir) {
			if (map.inBounds(p) && map[p] == 1) return true;
		}
		return false;
	}

	bool canAttack(const Unit& u, Point target) const
	{
		int32 dist = (u.pos - target).manhattanLength();
		const auto& stat = JobData[u.jobIndex];
		if (dist == 0 || dist > stat.rng) return false;

		if (isBlocked(u.pos, target)) return false;

		if (u.jobIndex == 2) {
			if (u.pos.x != target.x && u.pos.y != target.y) return false;
			Point dir = { 0, 0 };
			if (u.pos.x == target.x) dir.y = (target.y > u.pos.y) ? 1 : -1;
			else dir.x = (target.x > u.pos.x) ? 1 : -1;
			for (Point p = u.pos + dir; p != target; p += dir) {
				if (units.any([&](const Unit& n) { return n.pos == p; })) return false;
			}
		}
		return true;
	}

	void executeAction(Unit& actor)
	{
		int32 bestDist = 9999, targetIndex = -1;
		bool isHealer = (actor.jobIndex == 3);

		for (size_t i = 0; i < units.size(); ++i) {
			bool isAlly = (units[i].isPlayer == actor.isPlayer);
			if (isHealer) {
				if (isAlly && &units[i] != &actor && units[i].hp < JobData[units[i].jobIndex].hp) {
					if (canAttack(actor, units[i].pos)) {
						int32 d = (actor.pos - units[i].pos).manhattanLength();
						if (d < bestDist) { bestDist = d; targetIndex = (int32)i; }
					}
				}
			}
			else {
				if (!isAlly) {
					if (canAttack(actor, units[i].pos)) {
						int32 d = (actor.pos - units[i].pos).manhattanLength();
						if (d < bestDist) { bestDist = d; targetIndex = (int32)i; }
					}
				}
			}
		}

		if (isHealer && targetIndex == -1) {
			if (actor.hp < JobData[actor.jobIndex].hp) {
				for (size_t i = 0; i < units.size(); ++i) {
					if (&units[i] == &actor) { targetIndex = (int32)i; break; }
				}
			}
		}

		if (targetIndex != -1) {
			auto& t = units[targetIndex];
			int32 dmg = JobData[actor.jobIndex].atk;
			t.hp -= dmg;
			if (t.hp > JobData[t.jobIndex].hp) t.hp = JobData[t.jobIndex].hp;
			effect.add<EffectAnim>(t.pos * CellSize + Vec2{ 30,30 }, isHealer ? Palette::Lime : Palette::White, isHealer ? U"Heal" : U"Hit");
		}
	}

public:
	Game(const InitData& init) : IScene(init), map(Cols, Rows, 0)
	{
		Image img(CellSize, CellSize, ColorF{ 0, 0 });
		for (int i = -CellSize; i < CellSize * 2; i += 10) Line(i, 0, i - CellSize, CellSize).overwrite(img, 2, Palette::Red);
		slashTexture = Texture(img);

		for (auto& m : map) if (RandomBool(0.15)) m = 1;

		units << Unit({ 1, 2 }, true, 0) << Unit({ 1, 4 }, true, 2) << Unit({ 1, 6 }, true, 3) << Unit({ 2, 3 }, true, 1) << Unit({ 2, 5 }, true, 0);
		units << Unit({ 10, 2 }, false, 0) << Unit({ 10, 4 }, false, 3) << Unit({ 10, 6 }, false, 2) << Unit({ 9, 3 }, false, 0) << Unit({ 9, 5 }, false, 1);
		for (const auto& u : units) if (map.inBounds(u.pos)) map[u.pos] = 0;
	}

	void update() override
	{
		effect.update();
		units.remove_if([](const Unit& u) { return u.hp <= 0; });
		if (units.none([](const Unit& u) { return !u.isPlayer; })) { getData().resultMessage = U"WIN"; changeScene(U"Result"); }
		if (units.none([](const Unit& u) { return u.isPlayer; })) { getData().resultMessage = U"LOSE"; changeScene(U"Result"); }

		Point mp = Cursor::Pos() / CellSize;

		if (state == GameState::PlayerSelect) {
			if (MouseL.down()) {
				for (size_t i = 0; i < units.size(); ++i)
					if (units[i].pos == mp && units[i].isPlayer && !units[i].isActed) { selIdx = (int32)i; state = GameState::PlayerMove; }
			}
		}
		else if (state == GameState::PlayerMove) {
			if (MouseR.down()) { selIdx = -1; state = GameState::PlayerSelect; }
			else if (MouseL.down()) {
				bool switched = false;
				for (size_t i = 0; i < units.size(); ++i)
					if (units[i].pos == mp && units[i].isPlayer && !units[i].isActed && (int32)i != selIdx) { selIdx = (int32)i; switched = true; break; }

				if (!switched && selIdx != -1) {
					auto& u = units[selIdx];
					int32 dist = (u.pos - mp).manhattanLength();
					bool occupied = units.any([&](const Unit& n) { return n.pos == mp && n.pos != u.pos; });

					if (map.inBounds(mp) && map[mp] == 0 && dist <= JobData[u.jobIndex].mv && !occupied) {
						u.pos = mp; executeAction(u); u.isActed = true; selIdx = -1; state = GameState::PlayerSelect;
						if (units.all([](const Unit& n) { return !n.isPlayer || n.isActed; })) { state = GameState::EnemyTurn; enemyTimer = 0; }
					}
				}
			}
		}
		else if (state == GameState::EnemyTurn) {
			if ((enemyTimer += Scene::DeltaTime()) > 0.8) {
				for (auto& e : units) {
					if (!e.isPlayer && !e.isActed) {
						Point target = e.pos; int32 minD = 9999;
						for (const auto& p : units) if (p.isPlayer) {
							int32 d = (p.pos - e.pos).manhattanLength();
							if (d < minD) { minD = d; target = p.pos; }
						}
						Point best = e.pos; int32 bestD = minD;
						for (const auto& d : { Point{0,-1}, {0,1}, {-1,0}, {1,0} }) {
							Point n = e.pos + d;
							if (map.inBounds(n) && map[n] == 0 && !units.any([&](const Unit& u) { return u.pos == n && u.pos != e.pos; })) {
								int32 dist = (target - n).manhattanLength();
								if (dist < bestD) { bestD = dist; best = n; }
							}
						}
						e.pos = best; executeAction(e); e.isActed = true;
					}
				}
				for (auto& u : units) u.isActed = false;
				getData().turnCount++; state = GameState::PlayerSelect;
			}
		}
	}

	void draw() const override
	{
		Point mp = Cursor::Pos() / CellSize;
		for (auto p : step(Size(Cols, Rows))) {
			RectF cell{ p * CellSize, CellSize };
			cell.draw(map[p] == 1 ? Palette::Black : Palette::White).drawFrame(1, Palette::Lightgray);
			if (p == mp) cell.draw(ColorF{ 1, 1, 0, 0.2 });
			for (const auto& u : units) if (u.pos == p) cell.draw((u.isPlayer ? Palette::Skyblue : Palette::Pink).withAlpha(0.3));

			if (state == GameState::PlayerMove && selIdx != -1) {
				const auto& u = units[selIdx];
				int32 dist = (u.pos - p).manhattanLength();
				bool mv = (dist <= JobData[u.jobIndex].mv);
				if (map[p] == 0) {
					if (mv) cell.draw(ColorF{ 0, 0.5, 1, 0.3 });
					if (canAttack(u, p)) {
						if (mv) slashTexture.draw(cell.pos); else cell.draw(ColorF{ 1, 0, 0, 0.3 });
					}
				}
			}
		}
		for (const auto& u : units) u.draw();

		RectF ui{ 0, Rows * CellSize, static_cast<double>(Scene::Width()), 120.0 }; ui.draw(Palette::Black);
		FontAsset(U"M")(U"TURN: {}"_fmt(getData().turnCount)).draw(20, static_cast<int32>(ui.y + 10));

		if (state == GameState::EnemyTurn) {
			FontAsset(U"M")(U"Enemy Turn").draw(140, static_cast<int32>(ui.y + 10), Palette::Red);
		}
		else {
			FontAsset(U"M")(U"Player Turn").draw(140, static_cast<int32>(ui.y + 10), Palette::Cyan);
			String guide = (state == GameState::PlayerSelect) ? U"漢字を左クリックで選択" : U"右クリックでキャンセル";
			FontAsset(U"S")(guide).draw(280, static_cast<int32>(ui.y + 15), Palette::White);
		}

		const Unit* dU = nullptr;
		if (selIdx != -1) dU = &units[selIdx];
		for (const auto& u : units) if (u.pos == mp) dU = &u;
		if (dU) {
			const auto& d = JobData[dU->jobIndex];
			FontAsset(U"M")(U"{} HP:{}/{} 攻:{}"_fmt(d.name, dU->hp, d.hp, Abs(d.atk))).draw(Scene::Width() - 220, static_cast<int32>(ui.y + 10), Palette::Yellow);
		}

		int32 ty = static_cast<int32>(ui.y + 42);
		FontAsset(U"S")(U"青:味方 赤:敵  [兵:兵士 騎:騎士 銃:銃士 回:ヒーラー]").draw(20, ty);
		FontAsset(U"S")(U"選択時青部分：移動可能域　赤斜線・赤：攻撃範囲").draw(20, ty + 20);
		FontAsset(U"S")(U"・各HP/攻撃力はカーソルを合わせると表示される").draw(20, ty + 40);
	}
};

class Result : public App::Scene
{
public:
	Result(const InitData& init) : IScene(init) {}
	void update() override { if (MouseL.down()) changeScene(U"Title"); }
	void draw() const override {
		String msg = getData().resultMessage;
		FontAsset(U"T")(msg).drawAt(Scene::Center(), msg == U"LOSE" ? Palette::Red : Palette::Yellow);
		FontAsset(U"M")(U"クリックで戻る").drawAt(Scene::Center().movedBy(0, 70), Palette::White);
	}
};

void Main()
{
	FontAsset::Register(U"T", 60, Typeface::Heavy);
	FontAsset::Register(U"M", 20, Typeface::Bold);
	FontAsset::Register(U"F", 32, Typeface::Bold);
	FontAsset::Register(U"S", 15, Typeface::Regular);

	App m; m.add<Title>(U"Title"); m.add<Game>(U"Game"); m.add<Result>(U"Result");
	Scene::Resize(CellSize * Cols, CellSize * Rows + 120);
	while (System::Update()) if (!m.update()) break;
}
