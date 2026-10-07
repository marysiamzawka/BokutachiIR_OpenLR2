#include <LR2_customir_api.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

// Technically hook version, but let's think of it as API version :^)
// Although as the time of writing this we send more fields than BokutachiHook.
static constexpr struct version {
	int major = 2;
	int minor = 1;
	int patch = 2;
} version;

static std::filesystem::path path;
static std::string url;
static std::string urlDan;
static std::string apiKey;

constexpr const char* lamps[6] = { "NO PLAY", "FAIL", "EASY", "NORMAL", "HARD", "FULL COMBO" };
constexpr const char* gauges[6] = { "GROOVE", "HARD", "HAZARD", "EASY", "P-ATTACK", "G-ATTACK" };
constexpr const char* gameModes[8] = { "7K", "5K", "14K", "10K", "9K" };
constexpr const char* randomModes[6] = { "NORAN", "MIRROR", "RAN", "S-RAN", "H-RAN", "ALLSCR" };

static bool is_wine()
{
#ifdef _WIN32
	static const bool wine = []() {
		auto* ntdll = GetModuleHandle("ntdll");
		return ntdll != nullptr && static_cast<void*>(GetProcAddress(ntdll, "wine_get_version")) != nullptr;
	}();
	return wine;
#else
	return false;
#endif // _WIN32
}

static void Logger(std::string_view message)
{
	std::println("[BokutachiIR] {}", message);
	(void)fflush(stdout);

	std::ofstream logFile(path/"Bokutachi.log", std::ios_base::app);
	if (is_wine()) {
		logFile << std::format("[{:%d-%m-%Y %X}] {}\n", std::chrono::system_clock::now(), message);
	}
	else {
		auto const time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
		logFile << std::format("[{:%d-%m-%Y %X}] {}\n", time, message);
	}
}

static const char* OLR2_IR_API GetName() {
	return "BokutachiIR";
}

// \retval true Good
static bool CheckTachiApi() {
	std::string baseUrl = url.substr(0, url.find_first_of('/', 8));
	cpr::Response r = cpr::Get(cpr::Url{ baseUrl + "/api/v1/status" },
							   cpr::Timeout{ std::chrono::seconds(5) },
							   cpr::Bearer{ apiKey });

	if (r.error.code != cpr::ErrorCode::OK) {
		Logger(std::format("Couldn't GET: {}", r.error.message));
		return false;
	}

	try
	{
		json json = json::parse(r.text);
		if (json["body"]["whoami"] == nullptr) {
			Logger("Missing/Unknown API Key in 'BokutachiAuth.json'.");
			return false;
		}

		bool permissionsGood = false;
		for (auto& permission : json["body"]["permissions"]) {
			if (permission != "submit_score") continue;
			permissionsGood = true;
			break;
		}
		if (!permissionsGood) {
			Logger("API Key in BokutachiAuth.json is missing 'submit_score' permission.");
			return false;
		}
	}
	catch (json::exception& e)
	{
		Logger(std::format("JSON exception: {}", e.what()));
	}

	return true;
}

static bool OLR2_IR_API Login() {
	try {
		json config;
		{
			std::ifstream conf(path/"BokutachiAuth.json");
			config = json::parse(conf);
		}
		url = config.at("url");
		urlDan = url + "/course";
		apiKey = config.at("apiKey");
	}
	catch (const std::exception& e) {
		Logger("'BokutachiAuth.json' is missing or malformed.");
		return false;
	}
	return CheckTachiApi();
}

static const char* GetKeymode(int keymode) {
	switch (keymode) {
	case 7: return gameModes[0];
	case 5: return gameModes[1];
	case 14: return gameModes[2];
	case 10: return gameModes[3];
	case 9: return gameModes[4];
	default: return "INVALID";
	}
}

static std::string FormJSONString(const IRScoreV1& score) {
	const bool isCourse = score.state.isCourse;
	const bool hashIsCourse = score.song.hash.length() > 32;

	const std::chrono::seconds unixTimestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());

	const std::string md5 = hashIsCourse
			? std::string(score.song.hash.begin() + 32, score.song.hash.end())
			: score.song.hash;

	json scorePacket = {
		{"version", {
			{"major", version.major},
			{"minor", version.minor},
			{"patch", version.patch}
		}},
		{"unixTimestamp", unixTimestamp.count()},
		{"md5", md5},
		{"playerData", {
					{"autoScr", score.settings.assist[0] | score.settings.assist[1]},
					{"dpFlip", nullptr},
					{"gameMode", GetKeymode(score.state.keymode)},
					{"random", randomModes[score.settings.random[0]]},
					{"randomr", nullptr},
					{"gauge", gauges[score.gaugeType]},
					{"rseed", score.state.randomseed},
		}},
		{"scoreData", {
					{"pgreat", score.judgements_total.epg + score.judgements_total.lpg},
					{"great", score.judgements_total.egr + score.judgements_total.lgr},
					{"good", score.judgements_total.egd + score.judgements_total.lgd},
					{"bad", score.judgements_total.ebd + score.judgements_total.lbd},
					{"poor", score.judgements_total.epr + score.judgements_total.lpr},
					{"maxCombo", score.max_combo},
					{"exScore", score.exscore},
					{"moneyScore", score.moneyscore},
					{"notesTotal", score.state.notes_total},
					{"notesPlayed", score.judgements_total.notes_played},
					{"lamp", lamps[score.clearType]},
					{"hpGraph", score.graphs.hp[score.gaugeType]},
					{"extendedJudgements", {
							{"epg", score.judgements_total.epg},
							{"lpg", score.judgements_total.lpg},
							{"egr", score.judgements_total.egr},
							{"lgr", score.judgements_total.lgr},
							{"egd", score.judgements_total.egd},
							{"lgd", score.judgements_total.lgd},
							{"ebd", score.judgements_total.ebd},
							{"lbd", score.judgements_total.lbd},
							{"epr", score.judgements_total.epr},
							{"lpr", score.judgements_total.lpr},
							{"cb", score.judgements_total.cb},
							{"fast", score.judgements_total.fast},
							{"slow", score.judgements_total.slow},
							{"notesPlayed", score.judgements_total.notes_played}
						}
					},
					{"extendedHpGraphs", nullptr},
		}}
	};

	if (score.state.keymode == 10 || score.state.keymode == 14) {
		scorePacket["playerData"]["dpFlip"] = score.settings.dpflip == 1;
		scorePacket["playerData"]["randomr"] = randomModes[score.settings.random[1]];
	}

	if (!hashIsCourse && isCourse && score.clearType > 1)
	{
		scorePacket["scoreData"]["lamp"] = lamps[0];
	}

	if (score.settings.m_gas) {
		scorePacket["scoreData"]["extendedHpGraphs"] = {
			{"groove", score.graphs.hp[0]},
			{"hard", score.graphs.hp[1]},
			{"hazard", score.graphs.hp[2]},
			{"easy", score.graphs.hp[3]},
			{"pattack", score.graphs.hp[4]},
			{"gattack", score.graphs.hp[5]}
		};
	}
	return scorePacket.dump(4);
}

static SendScoreStatus OLR2_IR_API SendScore(const IRScoreV1& score) {
	const std::string reqBody = FormJSONString(score);
	const std::string songName = std::format("{} {}", score.song.title, score.song.subtitle);
	const bool hashIsCourse = score.song.hash.length() > 32;

	if (score.settings.is_extra) {
		Logger("Score not sent - Extra mode enabled");
		return SendScoreStatus::Fail;
	}

	if (score.state.isNosave) {
		Logger("Score not sent - NO SAVE score");
		return SendScoreStatus::Fail;
	}

	cpr::Response r = cpr::Post(cpr::Url{ hashIsCourse ? urlDan : url },
		cpr::Timeout{ std::chrono::seconds(30) },
		cpr::Header{ {"Content-Type", "application/json"} },
		cpr::Bearer{ apiKey },
		cpr::Body{ reqBody });

	if (r.error.code != cpr::ErrorCode::OK || r.status_code / 100 == 5) {
		Logger(std::format("Couldn't POST: {}", r.error.message));
		return SendScoreStatus::Retry;
	}

	try
	{
		json log = json::parse(r.text);
		if (!log["success"]) {
			Logger(std::format("Score for {} !success: {}", songName, std::string(log["description"])));
			Logger(reqBody);
			return SendScoreStatus::Fail;
		}
	}
	catch (json::exception& e)
	{
		Logger(std::format("JSON exception: {}", e.what()));
	}

	if (r.status_code != 200) {
		Logger(std::format("Score for {} !=200: {}", songName, r.status_line));
		Logger(reqBody);
		return SendScoreStatus::Fail;
	}

	return SendScoreStatus::Ok;
}

static std::string OLR2_IR_API GetWebRankingUrl(const char* songHash_) {
	std::string_view hash{songHash_};
	const bool hashIsCourse = hash.size() > 32;
	if (hashIsCourse) {
		Logger("Tachi doesn't have course rankings");
		return "";
	}

	cpr::Response r = cpr::Get(cpr::Url{ std::format("https://boku.tachi.ac/api/v1/search/chart-hash?search={}", hash) },
		cpr::Timeout{ std::chrono::seconds(5) });
	if (r.error.code != cpr::ErrorCode::OK || r.status_code / 100 == 5) {
		Logger(std::format("chart-hash request for {} failed: {}", hash, r.error.message));
		return "";
	}

	std::string chartId;
	std::string game;
	try
	{
		json log = json::parse(r.text);
		if (!log["success"]) {
			Logger(std::format("chart-hash request for {} !success: {}", hash, std::string(log["description"])));
			return "";
		}
		chartId = log["body"]["charts"][0]["chartID"];
		game = log["body"]["charts"][0]["game"];
	}
	catch (json::exception& e)
	{
		Logger(std::format("chart-hash request for {} JSON exception: {}", hash, e.what()));
		return "";
	}

	return std::format("https://boku.tachi.ac/games/{}/charts/{}", game, chartId);
}

// --- Rivals -----------------------------------------------------------------------------

// openlr2::IRRivalScore::clear uses the legacy LR2 0-5 clear scale (NoPlay..FullCombo), same
// ordering as openlr2::Lamp. Tachi's BMS/PMS lamp has more values than LR2's:
//   - any ASSIST-* lamp doesn't count as a real clear by BMS community convention, so it's
//     treated as a fail, not a clear.
//   - EX HARD CLEAR has no dedicated LR2 slot, so it collapses into Hard.
//   - PERFECT/MAX (finer-grained full-combo tiers, seen on scores imported from Beatoraja)
//     both collapse into FullCombo.
static int MapTachiLamp(const std::string& lamp) {
	if (lamp.find("ASSIST") != std::string::npos) return static_cast<int>(openlr2::Lamp::Fail);
	if (lamp == "FAILED") return static_cast<int>(openlr2::Lamp::Fail);
	if (lamp == "EASY CLEAR") return static_cast<int>(openlr2::Lamp::Easy);
	if (lamp == "CLEAR") return static_cast<int>(openlr2::Lamp::Groove);
	if (lamp == "HARD CLEAR" || lamp == "EX HARD CLEAR") return static_cast<int>(openlr2::Lamp::Hard);
	if (lamp == "FULL COMBO" || lamp == "PERFECT" || lamp == "MAX") return static_cast<int>(openlr2::Lamp::FullCombo);
	return static_cast<int>(openlr2::Lamp::NoPlay);
}

// Tachi v3 "game" identifiers relevant to LR2/OpenLR2. SyncRivalScores has no keymode
// parameter and must return every known score for the rival in one shot (score.hash alone
// identifies the chart/keymode on the OpenLR2 side), so every one of these is queried and
// merged every time.
// NOTE: Tachi has no BMS playtype for 5-key or 10-key charts at all, so those keymodes can
// never be populated via this module, by omission on Tachi's side rather than ours.
constexpr const char* kTachiGames[] = { "bms-7k", "bms-14k", "pms-keyboard", "pms-controller" };

template <typename T>
static T JsonOr(const json& obj, std::string_view key, T def) {
	if (!obj.is_object()) return def;
	const auto it = obj.find(std::string(key));
	if (it == obj.end() || it->is_null()) return def;
	try {
		return it->get<T>();
	}
	catch (json::exception&) {
		return def;
	}
}

static std::string BaseUrl() {
	return url.substr(0, url.find_first_of('/', 8));
}

// GETs {BaseUrl()}{path}, checks the Tachi `{success, body, description}` envelope, and on
// success hands back just `body`.
static bool HttpGetBody(const std::string& path, json& out) {
	cpr::Response r = cpr::Get(cpr::Url{ BaseUrl() + path },
		cpr::Timeout{ std::chrono::seconds(30) },
		cpr::Bearer{ apiKey });

	if (r.error.code != cpr::ErrorCode::OK || r.status_code / 100 == 5) {
		Logger(std::format("GET {} failed: {}", path, r.error.message));
		return false;
	}

	try {
		json parsed = json::parse(r.text);
		if (!JsonOr<bool>(parsed, "success", false)) {
			Logger(std::format("GET {} !success: {}", path, JsonOr<std::string>(parsed, "description", "?")));
			return false;
		}
		out = parsed["body"];
		return true;
	}
	catch (json::exception& e) {
		Logger(std::format("GET {} JSON exception: {}", path, e.what()));
		return false;
	}
}

static bool GetSelfId(long long& outId) {
	json status;
	if (!HttpGetBody("/api/v1/status", status)) {
		return false;
	}
	if (!status.contains("whoami") || status["whoami"].is_null()) {
		Logger("GetSelfId: API key isn't logged in (no whoami)");
		return false;
	}
	outId = status["whoami"].get<long long>();
	return true;
}

static openlr2::GetStatus OLR2_IR_API GetRivals(openlr2::IRRivalListResult& out) {
	long long selfId{};
	if (!GetSelfId(selfId)) {
		return openlr2::GetStatus::Fail;
	}

	// Rivals are tracked per-game on Tachi, not globally. bms-7k is used as the single
	// source of truth here since it's by far the most common OpenLR2/Bokutachi playtype;
	// rivals configured only under 14K/PMS on the Tachi side won't show up.
	json rivals;
	if (!HttpGetBody(std::format("/api/v1/users/{}/games/bms-7k/rivals", selfId), rivals)) {
		return openlr2::GetStatus::Fail;
	}
	if (!rivals.is_array()) {
		Logger("GetRivals: unexpected rivals response shape");
		return openlr2::GetStatus::Fail;
	}

	out.rivals.clear();
	for (const auto& u : rivals) {
		const int id = JsonOr<int>(u, "id", 0);
		const std::string name = JsonOr<std::string>(u, "username", "");
		if (id == 0 || name.empty()) {
			Logger("GetRivals: skipping malformed rival entry");
			continue;
		}
		out.rivals.push_back({ .id = id, .name = name });
	}
	out.fetched_at = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());

	Logger(std::format("GetRivals: {} rival(s)", out.rivals.size()));
	return openlr2::GetStatus::Ok;
}

static openlr2::GetStatus OLR2_IR_API SyncRivalScores(int rivalId, uint64_t /*lastUpdateHint*/, std::vector<openlr2::IRRivalScore>& out) {
	out.clear();
	bool anySucceeded = false;

	for (const char* game : kTachiGames) {
		json body;
		if (!HttpGetBody(std::format("/api/v1/users/{}/games/{}/pbs/all", rivalId, game), body)) {
			continue; // May simply mean this rival has no scores for this playtype.
		}
		anySucceeded = true;

		// chartID -> BMS MD5 hash, so pbs (keyed by chartID) can be matched to LR2's hash-keyed scores.
		std::unordered_map<std::string, std::string> chartHashById;
		for (const auto& chart : JsonOr<json>(body, "charts", json::array())) {
			const std::string chartId = JsonOr<std::string>(chart, "id", "");
			const std::string hash = JsonOr<std::string>(JsonOr<json>(chart, "data", json::object()), "hashMD5", "");
			if (!chartId.empty() && !hash.empty()) {
				chartHashById.emplace(chartId, hash);
			}
		}

		for (const auto& pb : JsonOr<json>(body, "pbs", json::array())) {
			const std::string chartId = JsonOr<std::string>(pb, "chartID", "");
			const auto hashIt = chartHashById.find(chartId);
			if (hashIt == chartHashById.end()) {
				continue;
			}

			const json scoreData = JsonOr<json>(pb, "scoreData", json::object());
			const json judgements = JsonOr<json>(scoreData, "judgements", json::object());
			const json optional = JsonOr<json>(scoreData, "optional", json::object());

			openlr2::IRRivalScore score{};
			score.hash = hashIt->second;
			score.clear = MapTachiLamp(JsonOr<std::string>(scoreData, "lamp", ""));
			score.pg = JsonOr<int>(judgements, "pgreat", 0);
			score.gr = JsonOr<int>(judgements, "great", 0);
			score.gd = JsonOr<int>(judgements, "good", 0);
			score.bd = JsonOr<int>(judgements, "bad", 0);
			score.pr = JsonOr<int>(judgements, "poor", 0);
			score.notes = score.pg + score.gr + score.gd + score.bd + score.pr;
			score.combo = JsonOr<int>(optional, "maxCombo", 0);
			score.minbp = JsonOr<int>(optional, "bp", -1);
			score.option = 0; // Not exposed by Tachi's PB documents.
			score.lastupdate = static_cast<uint64_t>(JsonOr<long long>(pb, "timeAchieved", 0) / 1000);
			out.push_back(std::move(score));
		}
	}

	Logger(std::format("SyncRivalScores({}): {} score(s)", rivalId, out.size()));
	return anySucceeded ? openlr2::GetStatus::Ok : openlr2::GetStatus::Fail;
}

// --- Per-chart leaderboard ----------------------------------------------------------------

// Resolves an LR2/BMS MD5 hash to Tachi's (game, chartID) pair. Same lookup GetWebRankingUrl
// already does; note the response key is "chartID", not "id" (that's a different key used by
// the /pbs/all charts array elsewhere in this file).
static bool ResolveChartByHash(const std::string& hash, std::string& game, std::string& chartId) {
	json body;
	if (!HttpGetBody(std::format("/api/v1/search/chart-hash?search={}", hash), body)) {
		return false;
	}
	if (!body.contains("charts") || !body["charts"].is_array() || body["charts"].empty()) {
		return false;
	}
	const auto& first = body["charts"][0];
	game = JsonOr<std::string>(first, "game", "");
	chartId = JsonOr<std::string>(first, "chartID", "");
	return !game.empty() && !chartId.empty();
}

static std::filesystem::path RankCacheDirectory() {
	return path / "IR";
}

static void WriteRankCache(const std::string& hash, const openlr2::IRRankResult& rank) {
	json entries = json::array();
	for (const auto& p : rank.ranking) {
		entries.push_back({
			{"id", p.id},
			{"name", p.name},
			{"clear", static_cast<int>(p.clear)},
			{"notes", p.notes},
			{"maxcombo", p.maxcombo},
			{"pg", p.pg}, {"gr", p.gr}, {"gd", p.gd}, {"bd", p.bd}, {"pr", p.pr},
			{"minbp", p.minbp},
			{"timestamp", p.timestamp},
		});
	}
	const json cache = {
		{"fetched_at", rank.lastupdate},
		{"my_rank", rank.myRank},
		{"total_player", rank.totalPlayer},
		{"entries", entries},
	};

	std::error_code ec;
	std::filesystem::create_directories(RankCacheDirectory(), ec);
	std::ofstream out(RankCacheDirectory() / (hash + ".json"));
	if (!out) {
		Logger(std::format("GetResultRank: failed to write rank cache for {}", hash));
		return;
	}
	out << cache.dump(2);
}

static openlr2::GetStatus OLR2_IR_API GetResultRank(const char* songHash_, int /*reserved*/, openlr2::IRRankResult& out) {
	const std::string hash{ songHash_ };
	out = {};

	// Course hashes are longer than a plain chart MD5; Tachi has no course rankings.
	if (hash.size() > 32) {
		Logger("GetResultRank: Tachi doesn't have course rankings");
		return openlr2::GetStatus::Fail;
	}

	std::string game, chartId;
	if (!ResolveChartByHash(hash, game, chartId)) {
		Logger(std::format("GetResultRank: couldn't resolve chart for hash {}", hash));
		return openlr2::GetStatus::Fail;
	}

	long long selfId{};
	if (!GetSelfId(selfId)) {
		return openlr2::GetStatus::Fail;
	}

	// My own rank/outOf for this chart. Non-fatal if missing (e.g. a brand new score that
	// hasn't propagated into ranking data yet) - the leaderboard below is still useful.
	json selfPbBody;
	if (HttpGetBody(std::format("/api/v1/users/{}/games/{}/pbs/{}", selfId, game, chartId), selfPbBody)) {
		const json rankingData = JsonOr<json>(JsonOr<json>(selfPbBody, "pb", json::object()), "rankingData", json::object());
		out.myRank = JsonOr<int>(rankingData, "rank", 0);
		out.totalPlayer = JsonOr<int>(rankingData, "outOf", 0);
	}

	// /pbs is fixed at 100 entries per page server-side; page via startRanking until a page
	// comes back short, to get the chart's entire leaderboard rather than just the top 100.
	// This is a deliberate choice over the unlimited /ir/beatoraja/charts/:sha256/scores
	// endpoint, which returns zeroed/faked judgements for LR2-sourced scores specifically
	// (see convert-scores.ts) - full-fidelity data in multiple requests beats fast but
	// fabricated data in one.
	constexpr int kPageSize = 100;
	out.ranking.clear();
	for (int startRanking = 1;; startRanking += kPageSize) {
		json boardBody;
		if (!HttpGetBody(std::format("/api/v1/games/{}/charts/{}/pbs?startRanking={}", game, chartId, startRanking), boardBody)) {
			// A failed page here still leaves earlier pages usable; stop rather than retry forever.
			break;
		}

		std::unordered_map<long long, std::string> usernameById;
		for (const auto& u : JsonOr<json>(boardBody, "users", json::array())) {
			usernameById[JsonOr<long long>(u, "id", 0)] = JsonOr<std::string>(u, "username", "");
		}

		const json pbs = JsonOr<json>(boardBody, "pbs", json::array());
		for (const auto& pb : pbs) {
			const long long userId = JsonOr<long long>(pb, "userID", 0);
			const auto nameIt = usernameById.find(userId);
			if (nameIt == usernameById.end()) {
				continue;
			}

			const json scoreData = JsonOr<json>(pb, "scoreData", json::object());
			const json judgements = JsonOr<json>(scoreData, "judgements", json::object());
			const json optional = JsonOr<json>(scoreData, "optional", json::object());

			openlr2::IRRankPlayer player{};
			player.name = nameIt->second;
			player.id = static_cast<int>(userId);
			player.timestamp = static_cast<uint64_t>(JsonOr<long long>(pb, "timeAchieved", 0) / 1000);
			player.clear = static_cast<openlr2::Lamp>(MapTachiLamp(JsonOr<std::string>(scoreData, "lamp", "")));
			player.pg = JsonOr<int>(judgements, "pgreat", 0);
			player.gr = JsonOr<int>(judgements, "great", 0);
			player.gd = JsonOr<int>(judgements, "good", 0);
			player.bd = JsonOr<int>(judgements, "bad", 0);
			player.pr = JsonOr<int>(judgements, "poor", 0);
			player.notes = player.pg + player.gr + player.gd + player.bd + player.pr;
			player.maxcombo = JsonOr<int>(optional, "maxCombo", 0);
			player.minbp = JsonOr<int>(optional, "bp", -1);
			out.ranking.push_back(std::move(player));
		}

		if (static_cast<int>(pbs.size()) < kPageSize) {
			break;
		}
	}

	// Tachi doesn't expose a true clear-distribution stat (NO PLAY/FAIL/EASY/.../FULL COMBO
	// counts across every player on the chart, not just the fetched page), so clearPlayers is
	// deliberately left at its zeroed default rather than reporting a partial/misleading count.
	out.lastupdate = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());

	WriteRankCache(hash, out);

	Logger(std::format("GetResultRank({}): rank {}/{}, {} entries", hash, out.myRank, out.totalPlayer, out.ranking.size()));
	return openlr2::GetStatus::Ok;
}

extern "C" OLR2_IR_EXPORT void OLR2_IR_API GetMethodTable(MethodTable& table) {
	table.GetName = &GetName;
	table.LoginV1 = &Login;
	table.SendScoreV1 = &SendScore;
	table.GetWebRankingUrl = &GetWebRankingUrl;
	table.GetRivals = &GetRivals;
	table.SyncRivalScores = &SyncRivalScores;
	table.GetResultRank = &GetResultRank;
}

#ifdef _WIN32
BOOL APIENTRY DllMain(
	HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH: {
		wchar_t modulePath[MAX_PATH]{};
		GetModuleFileNameW(hModule, modulePath, MAX_PATH);
		path = modulePath;
		path = path.parent_path();
		break;
	}

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;

	case DLL_PROCESS_DETACH:
		if (lpReserved != nullptr) {
			break;
		}
		break;
	}
	return TRUE;
}
#endif // _WIN32
