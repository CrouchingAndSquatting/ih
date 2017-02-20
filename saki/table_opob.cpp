#include "table_opob.h"

#include "libsaki/string_enum.h"
#include "libsaki/ai.h"
#include "libsaki/util.h"

#include <bitset>



TableOp::TableOp(TableOpOb &opOb, Who self) 
	: TableOperator(self)
	, mOpOb(opOb)
{
}

void TableOp::onActivated(Table &table)
{
	mOpOb.onActivated(mSelf, table);
}



Action makeAction(const string &actStr, const string &actArg, int who)
{
	using AC = ActCode;

	AC act = actCodeOf(actStr.c_str());
	int turn;
	switch (act) {
		case AC::SWAP_OUT:
		case AC::ANKAN:
			return Action(act, T37(actArg.c_str()));
		case AC::CHII_AS_LEFT:
		case AC::CHII_AS_MIDDLE:
		case AC::CHII_AS_RIGHT:
		case AC::PON:
		case AC::KAKAN:
		case AC::IRS_CHECK:
			return Action(act, std::stoi(actArg));
		case AC::IRS_RIVAL:
			turn = std::stoi(actArg);
			return Action(act, Who(who).byTurn(turn));
		default:
			return Action(act);
	}
}

unsigned createSwapMask(const TileCount &closed,
                        const std::vector<T37> &choices)
{
	// assume 'choices' is 34-sorted
	std::bitset<13> mask;
	int i = 0;

	auto it = choices.begin();
	for (const T37 &t : tiles37::ORDER37) {
		if (it == choices.end())
			break;
		int ct = closed.ct(t);
		if (ct > 0) {
			bool val = t.looksSame(*it);
			while (ct --> 0)
				mask[i++] = val;
			it += val; // consume choice if matched
		}
	}

	return mask.to_ulong();
}

std::vector<string> createTileStrs(const std::vector<T34> &ts)
{
	std::vector<string> res;
	for (T34 t : ts)
		res.emplace_back(t.str());
	return res;
}

std::string createTile(const T37 &t, bool lay = false)
{
	std::string res(t.str());
	if (lay)
		res += '_';
	return res;
}

json createTiles(const std::vector<T37> &ts)
{
	json res = json::array();
	for (const T37 &t : ts)
		res.emplace_back(createTile(t, false));
	return res;
}

json createBark(const M37 &m)
{
	json res;
	using T = M37::Type;
	T type = m.type();
	res["type"] = (type == T::CHII ? 1 : (type == T::PON ? 3 : 4));
	int open = m.layIndex();
	if (type != T::ANKAN)
		res["open"] = open;

	res["0"] = createTile(m[0], open == 0);
	res["1"] = createTile(m[1], open == 1);
	res["2"] = createTile(m[2], open == 2);

	if (m.isKan()) {
		res["3"] = createTile(m[3], type == T::KAKAN);
		res["isDaiminkan"] = (type == T::DAIMINKAN);
		res["isAnkan"] = (type == T::ANKAN);
		res["isKakan"] = (type == T::KAKAN);
	}

	return res;
}

json createBarks(const std::vector<M37> &ms)
{
	json list = json::array();
	for (const M37 &m : ms)
		list.emplace_back(createBark(m));
	return list;
}

template<typename T>
void rotate(T &arr)
{
	auto temp = arr[0];
	arr[0] = arr[1];
	arr[1] = arr[2];
	arr[2] = arr[3];
	arr[3] = temp;
}
                  



TableOpOb::TableOpOb(const std::array<int, 4> &girlIds)
	: mOps {
		TableOp(*this, Who(0)),
		TableOp(*this, Who(1)),
		TableOp(*this, Who(2)),
		TableOp(*this, Who(3))
	}
{
	RuleInfo rule;
	rule.roundLimit = 8;
	std::array<int, 4> points { 25000, 25000, 25000, 25000 };
	std::array<TableOperator*, 4> ops {
		&mOps[0], &mOps[1], &mOps[2], &mOps[3]
	};
	std::vector<TableObserver*> obs { this };
	Who td(0);

	mTable.reset(new Table(points, girlIds, ops, obs, rule, td));

    mTable->start();
}

void TableOpOb::onActivated(Who who, Table &table)
{
    using AC = ActCode;
    TableView view = table.getView(who);

	if (table.riichiEstablished(who) && view.iCanOnlySpin()) {
		json args;
		args["Who"] = who.index();
		system("riichi-auto", args);
		return;
	}

	int focusWho;
	if (view.iCan(AC::CHII_AS_LEFT)
			|| view.iCan(AC::CHII_AS_MIDDLE)
			|| view.iCan(AC::CHII_AS_RIGHT)
			|| view.iCan(AC::PON)
			|| view.iCan(AC::DAIMINKAN)
			|| view.iCan(AC::RON)) {
		focusWho = table.getFocus().who().turnFrom(who);
	} else {
		focusWho = -1;
	}

    json map;

	if (view.iCan(AC::SWAP_OUT)) {
		json mask;
		const TileCount &closed = table.getHand(who).closed();
		const auto &choices = view.mySwappables();
		map[stringOf(AC::SWAP_OUT)] = createSwapMask(closed, choices);
	}

	if (view.iCan(AC::ANKAN))
		map[stringOf(AC::ANKAN)] = createTileStrs(view.myAnkanables());

	if (view.iCan(AC::KAKAN))
		map[stringOf(AC::KAKAN)] = view.myKakanables();

	if (view.iCan(AC::IRS_CHECK)) {
		const Girl &girl = table.getGirl(who);
		int prediceCount = girl.irsCheckCount();
		json list = json::array();
		for (int i = 0; i < prediceCount; i++) {
			const IrsCheckRow &row = girl.irsCheckRow(i);
			json rmap;
			rmap["modelMono"] = row.mono;
			rmap["modelIndent"] = row.indent;
			rmap["modelText"] = row.name;
			rmap["modelAble"] = row.able;
			rmap["modelOn"] = row.on;
			list.emplace_back(rmap);
		}
		map[stringOf(AC::IRS_CHECK)] = list;
	}

	if (view.iCan(AC::IRS_RIVAL)) {
		const Girl &girl = table.getGirl(who);
		std::vector<int> tars;
		for (int i = 0; i < 4; i++)
			if (girl.irsRivalMask()[i])
				tars.push_back(Who(i).turnFrom(who));
		map[stringOf(AC::IRS_RIVAL)] = tars;
	}

	static const AC just[] = {
		AC::PASS, AC::SPIN_OUT,
		AC::CHII_AS_LEFT, AC::CHII_AS_MIDDLE, AC::CHII_AS_RIGHT,
		AC::PON, AC::DAIMINKAN, AC::RIICHI,
		AC::RON, AC::TSUMO, AC::RYUUKYOKU,
		AC::END_TABLE, AC::NEXT_ROUND, AC::DICE, AC::IRS_CLICK
	};

    for (AC code : just)
        if (view.iCan(code))
            map[stringOf(code)] = true;

    json args;
    args["action"] = map;
    args["lastDiscarder"] = focusWho;
	if (view.iForwardAll())
		args["green"] = true;
	peer(who.index(), "activated", args);
}

void TableOpOb::onTableStarted(const Table &table, uint32_t seed)
{
	(void) seed;
	onPointsChanged(table);
}

void TableOpOb::onFirstDealerChoosen(Who initDealer)
{
	json args;
	for (int w = 0; w < 4; w++) {
		args["dealer"] = initDealer.turnFrom(Who(w));
		peer(w, "first-dealer-choosen", args);
	}
}

void TableOpOb::onRoundStarted(int r, int e, Who d, 
                               bool al, int dp, uint32_t s)
{
	util::p("onRoundStarted", r, e, d.index(), 
            "al", al, "depo", dp, "seed", s);
	if (r > 100) { // prevent infinite offline-spin-out loop
		mEnd = true;
		return;
	}

	json args;
	args["round"] = r;
	args["extra"] = e;
	args["allLast"] = al;
	args["deposit"] = dp;
	for (int w = 0; w < 4; w++) {
		args["dealer"] = d.turnFrom(Who(w));
		peer(w, "round-started", args);
	}
}

void TableOpOb::onCleaned()
{
	broad("cleaned", json());
}

void TableOpOb::onDiced(const Table &table, int die1, int die2)
{
	json args;
	args["die1"] = die1;
	args["die2"] = die2;
	broad("diced", args);
}

void TableOpOb::onDealt(const Table &table)
{
	for (int w = 0; w < 4; w++) {
		const auto &init = table.getHand(Who(w)).closed().t37s(true);
		json args;
		args["init"] = createTiles(init);
		peer(w, "dealt", args);
	}
}

void TableOpOb::onFlipped(const Table &table)
{
	json args;
	args["newIndic"] = createTile(table.getMount().getDrids().back());
	broad("flipped", args);
}

void TableOpOb::onDrawn(const Table &table, Who who)
{
	const T37 &in = table.getHand(who).drawn();
	for (int w = 0; w < 4; w++) {
		json args;
		args["who"] = who.turnFrom(Who(w));
		if (table.duringKan())
			args["rinshan"] = true;
		if (w == who.index())
			args["tile"] = createTile(in);
		peer(w, "drawn", args);
	}
}

void TableOpOb::onDiscarded(const Table &table, bool spin)
{
	Who discarder = table.getFocus().who();
	const T37 &out = table.getFocusTile();
	bool lay = table.lastDiscardLay();

	json args;
	args["tile"] = createTile(out, lay);
	args["spin"] = spin;
	for (int w = 0; w < 4; w++) {
		args["who"] = discarder.turnFrom(Who(w));
		peer(w, "discarded", args);
	}
}

void TableOpOb::onRiichiCalled(Who who)
{
	for (int w = 0; w < 4; w++) {
		json args;
		args["who"] = who.turnFrom(Who(w));
		peer(w, "riichi-called", args);
	}
}

void TableOpOb::onRiichiEstablished(Who who)
{
	for (int w = 0; w < 4; w++) {
		json args;
		args["who"] = who.turnFrom(Who(w));
		peer(w, "riichi-established", args);
	}
}

void TableOpOb::onBarked(const Table &table, Who who, 
                         const M37 &bark, bool spin)
{
	Who from = bark.isCpdmk() ? table.getFocus().who() : Who();

	json args;
	args["actStr"] = stringOf(bark.type());
	args["bark"] = createBark(bark);
	args["spin"] = spin;
	for (int w = 0; w < 4; w++) {
		args["who"] = who.turnFrom(Who(w));
		args["fromWhom"] = from.somebody() ? from.turnFrom(Who(w)) : -1;
		peer(w, "barked", args);
	}
}

void TableOpOb::onRoundEnded(const Table &table, RoundResult result,
		                     const std::vector<Who> &openers, Who gunner,
		                     const std::vector<Form> &forms)
{
	using RR = RoundResult;

	// form and hand lists have same order as openers
	// but they don't need to be rotated since openers
	// are not rotated but changed by value
	json formsList = json::array();
	json handsList = json::array();

	for (Who who : openers) {
		const Hand &hand = table.getHand(who);

		json handMap;
		handMap["closed"] = createTiles(hand.closed().t37s(true));
		handMap["barks"] = createBarks(hand.barks());

		if (result == RR::TSUMO || result == RR::KSKP)
			handMap["pick"] = createTile(hand.drawn(), true);
		else if (result == RR::RON || result == RR::SCHR)
			handMap["pick"] = createTile(table.getFocusTile(), true);

		handsList.emplace_back(handMap);
	}

	for (size_t i = 0; i < forms.size(); i++) {
		const Form &form = forms[i];
		json formMap;
		formMap["spell"] = form.spell();
		formMap["charge"] = form.charge();
		formsList.emplace_back(formMap);
	}

	json args;
	args["result"] = stringOf(result);
	args["hands"] = handsList;
	args["forms"] = formsList;
	args["urids"] = createTiles(table.getMount().getUrids());
	for (int w = 0; w < 4; w++) {
		args["openers"] = json();
		for (Who who : openers)
			args["openers"].push_back(who.turnFrom(Who(w)));
		args["gunner"] = gunner.somebody() ? gunner.turnFrom(Who(w)) : -1;
		peer(w, "round-ended", args);
	}
}

void TableOpOb::onPointsChanged(const Table &table)
{
	json args;
	args["points"] = table.getPoints();
	for (int w = 0; w < 4; w++) {
		peer(w, "points-changed", args);
		rotate(args["points"]);
	}
}

void TableOpOb::onTableEnded(const std::array<Who, 4> &rank,
		                     const std::array<int, 4> &scores)
{
	mEnd = true;

	tableEndStat(rank);

	json args;
	args["scores"] = scores;
	for (int w = 0; w < 4; w++) {
		json rankList;
		for (Who who : rank)
			rankList.push_back(who.turnFrom(Who(w)));
		args["rank"] = rankList;
		peer(w, "table-ended", args);
		rotate(args["scores"]);
	}
}

void TableOpOb::onPoppedUp(const Table &table, Who who)
{
	json args;
	args["str"] = table.getGirl(who).popUpStr();
	peer(who.index(), "popped-up", args);
}

std::vector<Mail> TableOpOb::popMails()
{
	std::vector<Mail> res(mMails); // copy
	mMails.clear();
	return res;
}

bool TableOpOb::gameOver() const
{
	return mEnd;
}

void TableOpOb::action(int w, const string &actStr, const string &actArg)
{
	Who who(w);

	if (actStr == "SWEEP") {
		sweepOne(w);
	} else if (actStr == "BOT") {
		Girl::Id girlId = mTable->getGirl(who).getId();
		std::unique_ptr<Ai> ai(Ai::create(who, girlId));
		ai->onActivated(*mTable);
	} else {
		Action action = makeAction(actStr, actArg, w);
		mTable->action(who, action);
	}
}

void TableOpOb::sweepOne(int w)
{
	Who who(w);
	const auto &tifo = mTable->getTicketFolder(who);
	Action act = tifo.sweep();
	if (act.act() == ActCode::NOTHING)
		return;
	mTable->action(who, act);
}

std::vector<int> TableOpOb::sweepAll()
{
	std::array<Action, 4> actions;
	using AC = ActCode;
	for (int w = 0; w < 4; w++) {
		const auto &tifo = mTable->getTicketFolder(Who(w));
		actions[w] = tifo.sweep();
	}

	std::vector<int> res;
	for (int w = 0; w < 4; w++) {
		if (actions[w].act() != AC::NOTHING) {
			res.push_back(w);
			mTable->action(Who(w), actions[w]);
		}
	}
	return res;
}

void TableOpOb::tableEndStat(const std::array<Who, 4> &rank)
{
	json rankList;
	for (Who who : rank)
		rankList.push_back(who.index());

	json args;
	args["Rank"] = rankList;

	system("table-end-stat", args);
}

void TableOpOb::peer(int w, const char *event, const json &args)
{
	json msg;
	msg["Type"] = "table";
	msg["Event"] = event;
	msg["Args"] = args;
	mMails.emplace_back(w, msg.dump());
}

void TableOpOb::broad(const char *event, const json &args)
{
	json msg;
	msg["Type"] = "table";
	msg["Event"] = event;
	msg["Args"] = args;
	const auto &s = msg.dump();
	for (int w = 0; w < 4; w++)
		mMails.emplace_back(w, s);
}

void TableOpOb::system(const char *type, const json &args)
{
	json msg = args;
	msg["Type"] = type;
	const auto &s = msg.dump();
	mMails.emplace_back(-1, s);
}



