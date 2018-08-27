/**************************************************/
/*  Invictus 2018						          */
/*  Edsel Apostol                                 */
/*  ed_apostol@yahoo.com                          */
/**************************************************/

#include <algorithm>
#include "typedefs.h"
#include "search.h"
#include "engine.h"
#include "position.h"
#include "utils.h"
#include "bitutils.h"
#include "eval.h"
#include "log.h"
#include "movepicker.h"

namespace Search {
	int LMRTable[64][64];
	void initArr() {
		//LogAndPrintOutput logger;
		for (int d = 1; d < 64; d++) {
			for (int p = 1; p < 64; p++) {
				//LMRTable[d][p] = 0.75 + log(d) * log(p) / 2.25; //Ethereal
				//LMRTable[d][p] = 0.5 + log(d) * log(p) / 3; // Hannibal
				//LMRTable[d][p] = log(d) * log(p) / 1.95; // SF
				//LMRTable[d][p] = 0.5 + log(d) * log(p) / 2.1; // Laser
				LMRTable[d][p] = 0.75 + log(d) * log(p) / 2.25;
				//logger << " " << LMRTable[d][p];
			}
			//logger << "\n";
		}
	}
	inline int scoreFromTrans(int score, int ply, int mate) {
		return (score > mate) ? (score - ply) : ((score < -mate) ? (score + ply) : score);
	}
	inline int scoreToTrans(int score, int ply, int mate) {
		return (score > mate) ? (score + ply) : ((score < -mate) ? (score - ply) : score);
	}
}

using namespace Search;

void search_t::idleloop() {
	while (!exit_flag) {
		if (doSleep) wait();
		else {
			start();
			doSleep = true;
		}
	}
}

// use this for checking position routines: doMove and undoMove
uint64_t search_t::perft(int depth) {
	undo_t undo;
	uint64_t cnt = 0ull;
	if (depth == 0) return 1ull;
	movelist_t<256> mvlist;
	bool inCheck = pos.kingIsInCheck();
	if (inCheck) pos.genCheckEvasions(mvlist);
	else {
		pos.genTacticalMoves(mvlist);
		pos.genQuietMoves(mvlist);
	}
	uint64_t pinned = pos.pinnedPieces(pos.side);
	for (int x = 0; x < mvlist.size; ++x) {
		if (!pos.moveIsLegal(mvlist.mv(x), pinned, inCheck)) continue;
		pos.doMove(undo, mvlist.mv(x));
		cnt += perft(depth - 1);
		pos.undoMove(undo);
	}
	return cnt;
}

// use this for checking move generation: faster
uint64_t search_t::perft2(int depth) {
	movelist_t<256> mvlist;
	pos.genLegal(mvlist);
	if (depth == 1) return mvlist.size;
	undo_t undo;
	uint64_t cnt = 0ull;
	for (int x = 0; x < mvlist.size; ++x) {
		pos.doMove(undo, mvlist.mv(x));
		cnt += perft2(depth - 1);
		pos.undoMove(undo);
	}
	return cnt;
}

void search_t::extractPV(move_t rmove) {
	int ply = 0;
	undo_t undo[MAXPLYSIZE];
	pvlist.size = 0;
	pvlist.add(rmove);
	pos.doMove(undo[ply++], rmove);
	for (pv_hash_entry_t entry; e.pvt.retrievePV(pos.stack.hash, entry);) {
		uint64_t pinned = pos.pinnedPieces(pos.side);
		if (!pos.moveIsValid(entry.move, pinned)) break;
		if (!pos.moveIsLegal(entry.move, pinned, false)) break;
		pvlist.add(entry.move);
		entry.move.s = scoreToTrans(entry.move.s, ply, MATE);
		e.tt.store(pos.stack.hash, entry.move, entry.depth, TT_EXACT);
		pos.doMove(undo[ply++], entry.move);
		if (pos.isRepeat()) break;
		if (ply >= MAXPLY) break;
	}
	while (ply > 0) pos.undoMove(undo[--ply]);
}

void search_t::displayInfo(int depth, int alpha, int beta) {
	PrintOutput logger;
	uint64_t currtime = Utils::getTime() - e.start_time + 1;
	logger << "info depth " << depth << " seldepth " << maxplysearched;
	if (abs(rootmove.s) < (MATE - MAXPLY)) {
		if (rootmove.s <= alpha) logger << " score cp " << rootmove.s << " upperbound";
		else if (rootmove.s >= beta) logger << " score cp " << rootmove.s << " lowerbound";
		else logger << " score cp " << rootmove.s;
	}
	else
		logger << " score mate " << ((rootmove.s > 0) ? (MATE - rootmove.s + 1) / 2 : -(MATE + rootmove.s) / 2);
	uint64_t totalnodes = e.nodesearched();
	logger << " time " << currtime << " nodes " << totalnodes << " nps " << (totalnodes * 1000 / currtime) << " pv";
	for (int idx = 0; idx < pvlist.size; ++idx) logger << " " << pvlist.mv(idx).to_str();
}

void search_t::start() {
	memset(history, 0, sizeof(history));
	for (auto& m : killer1) m = 0;
	for (auto& m : killer2) m = 0;
	nodecnt = 0;
	bool inCheck = pos.kingIsInCheck();

	for (int depth = 1; depth <= e.limits.depth; ++depth) {
		int delta = 16;
		e.alpha = -MATE;
		e.beta = MATE;
		maxplysearched = 0;
		if (depth > 3)
			e.alpha = std::max(-MATE, e.rootbestmove.s - delta), e.beta = std::min(MATE, e.rootbestmove.s + delta);
		while (true) {
			//PrintOutput() << thread_id << " : " << depth << " " << e.alpha << " " << e.beta;
			stop_iter = false;
			resolve_iter = false;
			search(true, true, e.alpha, e.beta, depth, 0, inCheck);
			if (e.stop) break;
			std::lock_guard<spinlock_t> lock(e.updatelock);
			if (stop_iter) {
				if (resolve_iter) continue;
				else break;
			}
			if (rootmove.s <= e.alpha) e.alpha = std::max(-MATE, rootmove.s - delta);
			else if (rootmove.s >= e.beta) e.beta = std::min(MATE, rootmove.s + delta);
			else {
				extractPV(rootmove);
				if (depth > e.rootbestdepth || (depth == e.rootbestdepth && rootmove.s > e.rootbestmove.s)) {
					e.rootbestdepth = depth;
					e.rootbestmove = rootmove;
					if (pvlist.size > 1) e.rootponder = pvlist.mv(1);
					if (depth >= 8) {
						//PrintOutput() << "thread_id: " << thread_id;
						displayInfo(depth, e.alpha, e.beta);
					}
					e.stopIteration();
				}
				break;
			}
			delta <<= 1;
			e.resolveIteration();
			e.stopIteration();
		}
		if (e.stop) break;
		// TODO check for time here if going to next iter is still possible, if > 70% stop search
	}

	if (!e.stop && (e.limits.ponder || e.limits.infinite)) {
		while (!e.use_time && !e.stop)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	if (thread_id == 0) {
		LogAndPrintOutput logger;
		logger << "bestmove " << e.rootbestmove.to_str();
		if (pvlist.size >= 2) logger << " ponder " << e.rootponder.to_str();
	}
}

bool search_t::stopSearch() {
	if ((++nodecnt & 0x3fff) == 0) {
		if (thread_id == 0 && e.use_time) {
			uint64_t currtime = Utils::getTime();
			if ((currtime >= e.time_limit_max && !resolve_iter) || (currtime >= e.time_limit_abs)) {
				e.stop = true;
				return true;
			}
		}
	}
	return false;
}

int search_t::search(bool root, bool inPv, int alpha, int beta, int depth, int ply, bool inCheck) {
	if (depth <= 0) return qsearch(alpha, beta, ply, inCheck);

	ASSERT(alpha < beta);
	ASSERT(!(pos.colorBB[WHITE] & pos.colorBB[BLACK]));

	if (stopSearch()) return 0;

	if (!root) {
		if (ply > maxplysearched) maxplysearched = ply;
		if (pos.stack.fifty > 99 || pos.isRepeat() || pos.isMatDrawn()) return 0;
		if (ply >= MAXPLY) return eval.score(pos);
		alpha = std::max(alpha, -MATE + ply);
		beta = std::min(beta, MATE - ply - 1);
		if (alpha >= beta) return alpha;
	}

	tt_entry_t tte;
	tte.move.m = 0;
	if (e.tt.retrieve(pos.stack.hash, tte)) {
		tte.move.s = scoreFromTrans(tte.move.s, ply, MATE);
		if (!inPv && tte.depth >= depth && ((tte.bound() == TT_EXACT) ||
			(tte.bound() == TT_LOWER && tte.move.s >= beta)
			|| (tte.bound() == TT_UPPER && tte.move.s <= alpha)))
			return tte.move.s;
	}

	if (!inPv && !inCheck
		&& depth >= 2 && pos.colorBB[pos.side] & ~(pos.piecesBB[PAWN] | pos.piecesBB[KING])
		&& pos.stack.lastmove.m != 0 && tte.move.m == 0) {
		int evalscore = eval.score(pos);
		if (evalscore >= beta) {
			undo_t undo;
			int R = 4 + depth / 6 + std::min(3, (evalscore - beta) / 200);
			pos.doNullMove(undo);
			int score = -search(false, false, -beta, -beta + 1, depth - R, ply + 1, false);
			pos.undoNullMove(undo);
			if (e.stop || stop_iter) return 0;
			if (score >= beta) {
				if (score >= 32500) score = beta;
				if (depth < 12 && abs(beta) < 32500) return score;
				int score2 = search(false, false, alpha, beta, depth - R, ply + 1, inCheck);
				if (e.stop || stop_iter) return 0;
				if (score2 >= beta) return score;
			}
		}
	}

	if (inPv && !inCheck && tte.move.m == 0 && depth >= 3) {
		search(root, inPv, alpha, beta, depth - 2, ply, inCheck);
		if (e.stop || stop_iter) return 0;
		e.tt.retrieve(pos.stack.hash, tte);
	}

	int old_alpha = alpha;
	int best_score = -MATE;
	int movestried = 0;
	move_t best_move(0);
	undo_t undo;
	int score;
	uint32_t move_hash;
	movepicker_t mp(*this, inCheck, false, ply, tte.move.m, killer1[ply], killer2[ply]);
	uint64_t dcc = pos.discoveredCheckCandidates(pos.side);
	playedmoves[ply].size = 0;
	for (move_t m; mp.getMoves(m);) {
		if (e.doSMP && mp.stage == STG_DEFERRED) movestried = m.s;
		else ++movestried;

		if (e.doSMP && mp.stage != STG_DEFERRED && best_score != -MATE) {
			if (mp.deferred.size > 0 && depth >= e.CUTOFF_CHECK_DEPTH) {
				if (e.tt.retrieve(pos.stack.hash, tte)) {
					tte.move.s = scoreFromTrans(tte.move.s, ply, MATE);
					if (tte.depth >= depth && ((tte.bound() == TT_EXACT) ||
						(tte.bound() == TT_LOWER && tte.move.s >= beta)
						|| (tte.bound() == TT_UPPER && tte.move.s <= old_alpha)))
						return tte.move.s;
				}
			}
			move_hash = pos.stack.hash >> 32;
			move_hash ^= (m.m * 1664525) + 1013904223;
			if (e.deferMove(move_hash, depth)) {
				m.s = movestried;
				mp.deferred.add(m);
				continue;
			}
		}

		bool moveIsCheck = pos.moveIsCheck(m, dcc);

		if (best_score == -MATE) {
			pos.doMove(undo, m);
			score = -search(false, inPv, -beta, -alpha, depth - 1 + moveIsCheck, ply + 1, moveIsCheck);
			pos.undoMove(undo);
		}
		else {
			int reduction = 1;
			if (!inCheck && !moveIsCheck && !pos.moveIsTactical(m) && depth > 2) {
				reduction = LMRTable[std::min(depth, 63)][std::min(movestried, 63)];
				reduction += !inPv;
				reduction -= (m.m == mp.killer1) || (m.m == mp.killer2);
				reduction = std::min(depth - 1, std::max(reduction, 1));
			}

			pos.doMove(undo, m);

			if (e.doSMP && mp.stage != STG_DEFERRED) e.startingSearch(move_hash, depth);
			score = -search(false, false, -alpha - 1, -alpha, depth - reduction, ply + 1, moveIsCheck);
			if (e.doSMP && mp.stage != STG_DEFERRED) e.finishedSearch(move_hash, depth);

			if (reduction != 1 && !e.stop && !stop_iter && score > alpha)
				score = -search(false, false, -alpha - 1, -alpha, depth - 1, ply + 1, moveIsCheck);

			if (inPv && !e.stop && !stop_iter && score > alpha)
				score = -search(false, inPv, -beta, -alpha, depth - 1, ply + 1, moveIsCheck);

			pos.undoMove(undo);
		}
		if (e.stop || stop_iter) return 0;

		if (movestried <= 64) playedmoves[ply].add(m);

		if (score > best_score) {
			best_score = score;
			if (root) {
				rootmove = m;
				rootmove.s = best_score;
			}
			if (score > alpha) {
				best_move = m;
				best_move.s = score;
				if (score >= beta) break;
				alpha = score;
			}
		}
	}
	if (movestried == 0) {
		if (inCheck) return -MATE + ply;
		else return 0;
	}
	if (best_score > old_alpha) {
		updateHistory(pos, best_move, depth, ply);
		if (best_score >= beta) {
			best_move.s = scoreToTrans(best_score, ply, MATE);
			e.tt.store(pos.stack.hash, best_move, depth, TT_LOWER);
			if (killer1[ply] != best_move.m) {
				killer2[ply] = killer1[ply];
				killer1[ply] = best_move.m;
			}
		}
		else {
			e.pvt.storePV(pos.stack.hash, best_move, depth);
			best_move.s = scoreToTrans(best_score, ply, MATE);
			e.tt.store(pos.stack.hash, best_move, depth, TT_EXACT);
		}
	}
	else {
		best_move.s = scoreToTrans(best_score, ply, MATE);
		e.tt.store(pos.stack.hash, best_move, depth, TT_UPPER);
	}
	return best_score;
}

int search_t::qsearch(int alpha, int beta, int ply, bool inCheck) {
	ASSERT(alpha < beta);
	ASSERT(!(pos.colorBB[WHITE] & pos.colorBB[BLACK]));

	if (stopSearch()) return 0;

	if (ply > maxplysearched) maxplysearched = ply;
	if (pos.stack.fifty > 99 || pos.isRepeat() || pos.isMatDrawn()) return 0;
	if (ply >= MAXPLY) return eval.score(pos);

	int best_score = -MATE;
	movepicker_t mp(*this, inCheck, true, ply);
	if (!inCheck) {
		best_score = eval.score(pos);
		if (best_score >= beta) return best_score;
		alpha = std::max(alpha, best_score);
	}
	undo_t undo;
	move_t best_move(0);
	int movestried = 0;
	uint64_t dcc = pos.discoveredCheckCandidates(pos.side);
	for (move_t m; mp.getMoves(m);) {
		++movestried;
		bool moveGivesCheck = pos.moveIsCheck(m, dcc);
		pos.doMove(undo, m);
		int score = -qsearch(-beta, -alpha, ply + 1, moveGivesCheck);
		pos.undoMove(undo);
		if (e.stop || stop_iter) return 0;
		if (score > best_score) {
			best_move = m;
			best_move.s = best_score = score;
			if (score > alpha) {
				if (score >= beta) return score;
				alpha = score;
			}
		}
	}
	if (movestried == 0 && inCheck) return -MATE + ply;
	if (best_move.m != 0) e.pvt.storePV(pos.stack.hash, best_move, 0);
	return best_score;
}

void search_t::updateHistory(position_t& p, move_t bm, int depth, int ply) {
	depth = std::min(15, depth);
	history[p.side][p.getPiece(bm.moveFrom())][bm.moveTo()] += depth * depth;
	for (int idx = 0; idx < playedmoves[ply].size; ++idx) {
		move_t m = playedmoves[ply].mv(idx);
		if (p.moveIsTactical(m)) continue;
		if (m == bm) continue;
		int& sc = history[p.side][p.getPiece(m.moveFrom())][m.moveTo()];
		sc -= sc / (30 - depth); // decay
	}
}