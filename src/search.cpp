/**************************************************/
/*  Invictus 2021                                 */
/*  Edsel Apostol                                 */
/*  ed_apostol@yahoo.com                          */
/**************************************************/

#include <algorithm>
#include <cmath>
#include "typedefs.h"
#include "search.h"
#include "engine.h"
#include "position.h"
#include "utils.h"
#include "bitutils.h"
#include "eval.h"
#include "log.h"
#include "movepicker.h"
#include "params.h"

namespace Search {
    static const int LMPTable[9] = { 0, 3, 5, 7, 15, 21, 27, 35, 43 };
    int LMRTable[64][64];
    void initArr() {
        //LogAndPrintOutput logger;
        for (int d = 1; d < 64; d++) {
            for (int p = 1; p < 64; p++) {
                LMRTable[d][p] = 0.75 + log(d) * log(p) / 2.1;
                //logger << " " << LMRTable[d][p];
            }
            //logger << "\n";
        }
    }
    inline int scoreFromTrans(int score, int ply, int mate) {
        return (score >= mate) ? (score - ply) : ((score <= -mate) ? (score + ply) : score);
    }
    inline int scoreToTrans(int score, int ply, int mate) {
        return (score >= mate) ? (score + ply) : ((score <= -mate) ? (score - ply) : score);
    }
    inline void updatePV(movelist_t<128> pv[], move_t& m, int ply) {
        pv[ply].size = 0;
        pv[ply].add(m);
        for (auto mm : pv[ply + 1]) pv[ply].add(mm);
    }
}

using namespace Search;
using namespace EvalParam;

void search_t::idleloop() {
    while (!exit_flag) {
        if (do_sleep) wait();
        else {
            start();
            do_sleep = true;
        }
    }
}

// use this for checking position routines: doMove and undoMove
uint64_t search_t::perft(size_t depth) {
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
    uint64_t pinned = pos.pinnedPiecesBB(pos.side);
    for (move_t m : mvlist) {
        if (!pos.moveIsLegal(m, pinned, inCheck)) continue;
        pos.doMove(undo, m);
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
    for (move_t m : mvlist) {
        pos.doMove(undo, m);
        cnt += perft2(depth - 1);
        pos.undoMove(undo);
    }
    return cnt;
}

void search_t::updateInfo() {
    uint64_t currtime = Utils::getTime() - e.start_time + 1;
    uint64_t totalnodes = e.nodesearched();
    PrintOutput() << "info time " << currtime << " nodes " << totalnodes << " nps " << (totalnodes * 1000 / currtime);
}

void search_t::displayInfo(move_t bestmove, int depth, int alpha, int beta) {
    PrintOutput logger;
    uint64_t currtime = Utils::getTime() - e.start_time + 1;
    logger << "info depth " << depth << " seldepth " << maxplysearched;
    if (abs(bestmove.s) < MATE - MAXPLY) {
        if (bestmove.s <= alpha) logger << " score cp " << bestmove.s << " upperbound";
        else if (bestmove.s >= beta) logger << " score cp " << bestmove.s << " lowerbound";
        else logger << " score cp " << bestmove.s;
    }
    else
        logger << " score mate " << ((bestmove.s > 0) ? (MATE - bestmove.s + 1) / 2 : -(MATE + bestmove.s) / 2);
    uint64_t totalnodes = e.nodesearched();
    logger << " time " << currtime << " nodes " << totalnodes << " nps " << (totalnodes * 1000 / currtime) << " pv";
    for (move_t m : pvlist[0]) logger << " " << m.to_str();
}

void search_t::start() {
    if (e.doNUMA) Utils::bindThisThread(thread_id); // NUMA bindings

    memset(history, 0, sizeof(history));
    memset(countermove, 0, sizeof(countermove));
    memset(killer1, 0, sizeof(killer1));
    memset(killer2, 0, sizeof(killer2));
    nodecnt = 0;
    bool inCheck = pos.kingIsInCheck();
    int last_score = 0;
    int mate_count = 0;

    for (rdepth = e.rdepth; rdepth <= e.limits.depth; rdepth = e.rdepth) {
        int delta = 10;
        maxplysearched = 0;
        while (true) {
            stop_iter = false;
            search(true, true, e.alpha, e.beta, rdepth, 0, inCheck);
            if (e.stop || e.plysearched[rdepth - 1]) break;
            else if (stop_iter && e.resolve_iter) continue;
            else {
                std::lock_guard<spinlock_t> lock(e.updatelock);
                if (e.stop || e.plysearched[rdepth - 1]) break;
                else if (stop_iter && e.resolve_iter) continue;
                if (rootmove.s <= e.alpha)
                    e.beta = (e.alpha + e.beta) / 2,
                    e.alpha = std::max(-MATE, rootmove.s - delta);
                else if (rootmove.s >= e.beta)
                    e.beta = std::min(MATE, rootmove.s + delta);
                else {
                    e.plysearched[rdepth - 1] = true;
                    e.resolve_iter = false;
                    e.rootbestmove = rootmove;
                    if (pvlist[0].size > 1) e.rootponder = pvlist[0].mv(1);
                    if (rdepth >= 8) displayInfo(rootmove, rdepth, e.alpha, e.beta);
                    e.rdepth = ++rdepth;
                    if (rdepth >= 5)
                        e.alpha = std::max(-MATE, e.rootbestmove.s - delta),
                        e.beta = std::min(MATE, e.rootbestmove.s + delta);
                    else
                        e.alpha = -MATE,
                        e.beta = MATE;
                    e.stopIteration();
                    break;
                }
                delta += delta / 2;
                e.resolve_iter = true;
                e.stopIteration();
            }
        }
        if (e.stop) break;
        if (thread_id == 0 && e.use_time) {
            int64_t currtime = Utils::getTime();
            if (currtime - e.start_time >= ((e.time_limit_max - e.start_time) * 7) / 10) {
                if (last_score <= e.rootbestmove.s - 30)
                    e.time_limit_max = std::min(e.time_limit_max + e.time_range / 2, e.time_limit_abs);
                else
                    break;
            }
            last_score = e.rootbestmove.s;
            if (rdepth >= 16 && abs(last_score) > MATE - MAXPLY) ++mate_count;
            if (mate_count >= 4) break;
        }
    }

    if (!e.stop && (e.limits.ponder || e.limits.infinite)) {
        while (!e.use_time && !e.stop) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    else e.stopthreads();

    if (thread_id == 0) {
        updateInfo();
        LogAndPrintOutput logger;
        logger << "bestmove " << e.rootbestmove.to_str();
        if (pvlist[0].size > 1) logger << " ponder " << e.rootponder.to_str();
    }
}

bool search_t::stopSearch() {
    ++nodecnt;
    if (thread_id == 0 && e.use_time && (nodecnt & 0x3fff) == 0) {
        int64_t currtime = Utils::getTime();
        if ((currtime >= e.time_limit_max && !e.resolve_iter) || (currtime >= e.time_limit_abs)) {
            if (e.rootbestmove.m == 0)
                e.time_limit_max = std::min(e.time_limit_max + e.time_range / 2, e.time_limit_abs);
            else
                e.stop = true;
        }
    }
    if (thread_id == 0 && (nodecnt & 0x3fffff) == 0) updateInfo();
    return e.stop;
}

int search_t::search(bool inRoot, bool inPv, int alpha, int beta, int depth, int ply, bool inCheck) {
    if (depth <= 0) return qsearch(inPv, alpha, beta, ply, inCheck);

    ASSERT(alpha < beta);
    ASSERT(!(pos.colorBB[WHITE] & pos.colorBB[BLACK]));

    pvlist[ply].size = 0;

    if (!inRoot) {
        if (stopSearch()) return 0;
        if (inPv && ply > maxplysearched) maxplysearched = ply;
        if (pos.stack.fifty > 99 || pos.isRepeat() || pos.isMatDrawn()) return 0;
        if (ply >= MAXPLY) return et.retrieve(pos);
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    tt_entry_t tte;
    tte.move.m = 0;
    if (e.tt.retrieve(pos.stack.hash, tte)) {
        tte.move.s = scoreFromTrans(tte.move.s, ply, MATE - MAXPLY);
        if (!inRoot && !inPv && tte.depth >= depth && (tte.getBound() == TT_EXACT
            || (tte.getBound() == TT_LOWER && tte.move.s >= beta)
            || (tte.getBound() == TT_UPPER && tte.move.s <= alpha)))
            return tte.move.s;
    }

    int evalscore = et.retrieve(pos); // TODO: optimize
    const bool nonpawnpcs = pos.colorBB[pos.side] & ~(pos.piecesBB[PAWN] | pos.piecesBB[KING]);

    if (!inRoot && !inPv && !inCheck) {
        if (depth < 2 && evalscore + 325 < alpha) // TODO: test
            return qsearch(inPv, alpha, beta, ply, inCheck);
        if (depth < 9 && evalscore - 85 * depth > beta) // TODO: test
            return evalscore;
        if (depth >= 2 && evalscore >= beta && nonpawnpcs && pos.stack.lastmove.m != 0 && tte.move.m == 0) {
            undo_t undo;
            int R = ((13 + depth) >> 2) + std::min(3, (evalscore - beta) / 185);
            pos.doNullMove(undo);
            int score = -search(false, false, -beta, -beta + 1, depth - R, ply + 1, false);
            pos.undoNullMove(undo);
            if (e.stop || stop_iter) return 0;
            if (score >= beta) {
                if (score >= MATE - MAXPLY) score = beta;
                if (depth < 12 && abs(beta) < MATE - MAXPLY) return score;
                int score2 = search(false, false, alpha, beta, depth - R, ply + 1, inCheck);
                if (e.stop || stop_iter) return 0;
                if (score2 >= beta) return score;
            }
        }
        if (depth > 4 && std::abs(beta) < MATE - MAXPLY) {
            undo_t undo;
            int rbeta = std::min(beta + 100, MATE);
            uint64_t dcc = pos.discoveredPiecesBB(pos.side);
            movepicker_t mp(*this, inCheck, true, rbeta - evalscore);
            for (move_t m; mp.getMoves(m);) {
                bool moveGivesCheck = pos.moveIsCheck(m, dcc);
                pos.doMove(undo, m);
                int score = -qsearch(inPv, -rbeta, -rbeta + 1, ply + 1, moveGivesCheck);
                if (score >= rbeta) score = -search(false, false, -rbeta, -rbeta + 1, depth - 4, ply + 1, moveGivesCheck);
                pos.undoMove(undo);
                if (e.stop || stop_iter) return 0;
                if (score >= rbeta) return score;
            }
        }
    }
    const int futilityMargin = evalscore + (90 * depth) + 250; // TODO: test
    int old_alpha = alpha;
    int best_score = -MATE;
    int movestried = 0;
    move_t best_move(0);
    undo_t undo;
    int score;
    uint32_t move_hash;
    move_t lm = pos.stack.lastmove;
    uint16_t cm = countermove[pos.side][pos.getPiece(lm.moveTo())][lm.moveTo()];
    movepicker_t mp(*this, inCheck, false, 1, tte.move.m, killer1[ply], killer2[ply], cm);
    uint64_t dcc = pos.discoveredPiecesBB(pos.side);
    bool skipquiets = false;
    playedmoves[ply].size = 0;
    for (move_t m; mp.getMoves(m, skipquiets);) {
        if (e.doSMP && mp.stage == STG_DEFERRED) movestried = m.s;
        else ++movestried;

        bool moveGivesCheck = pos.moveIsCheck(m, dcc);

        if (best_score == -MATE) {
            int extension = 0;
            if (moveGivesCheck) extension = 1;
            else if (inCheck && mp.mvlist.size == 1) extension = 1;
            else if (!inRoot && depth >= 8 && tte.move.m == m.m && tte.depth >= depth - 2 && tte.getBound() == TT_LOWER) {
                int xbeta = std::max(tte.move.s - depth * 2, -MATE), xscore = -MATE;
                movepicker_t mpx(*this, inCheck, false, 1, tte.move.m, killer1[ply], killer2[ply], cm);
                for (move_t mx; mpx.getMoves(mx, false);) {
                    if (mx.m == tte.move.m) continue;
                    bool givesCheck = pos.moveIsCheck(mx, dcc);
                    pos.doMove(undo, mx);
                    xscore = -search(false, inPv, -xbeta - 1, -xbeta, depth / 2 - 1, ply + 1, givesCheck);
                    pos.undoMove(undo);
                    if (e.stop || stop_iter) return 0;
                    if (xscore >= xbeta) break;
                }
                if (xscore != -MATE && xscore < xbeta) extension = 1;
            }
            pos.doMove(undo, m);
            score = -search(false, inPv, -beta, -alpha, depth - 1 + extension, ply + 1, moveGivesCheck);
            pos.undoMove(undo);
        }
        else {
            if (e.doSMP && mp.stage != STG_DEFERRED && depth >= e.defer_depth) {
                if (!inRoot && !inPv && mp.deferred.size > 0 && depth >= e.cutoffcheck_depth) {
                    tt_entry_t ttet;
                    if (e.tt.retrieve(pos.stack.hash, ttet)) {
                        int tscore = scoreFromTrans(tte.move.s, ply, MATE - MAXPLY);
                        if (ttet.depth >= depth && (ttet.getBound() == TT_EXACT
                            || (ttet.getBound() == TT_LOWER && tscore >= beta)
                            || (ttet.getBound() == TT_UPPER && tscore <= old_alpha)))
                            return tscore;
                    }
                }
                move_hash = pos.stack.hash >> 32;
                move_hash ^= (m.m * 1664525) + 1013904223;
                if (e.mht.isBusy(move_hash, depth)) {
                    m.s = movestried;
                    mp.deferred.add(m);
                    continue;
                }
            }
            // TODO: Counter moves history, Follow up moves history
            bool isTactical = pos.moveIsTactical(m);
            if (!inRoot && !inPv && !inCheck && !moveGivesCheck && nonpawnpcs && depth < 9 && mp.stage != STG_DEFERRED) {
                if (!isTactical && futilityMargin <= alpha) { skipquiets = true; continue; }
                if (!isTactical && movestried >= LMPTable[depth]) { skipquiets = true; continue; }
                if ((!isTactical || mp.stage == STG_BADTACTICS) && !pos.statExEval(m, isTactical ? -100 * depth : -10 * depth * depth)) continue;
            }

            pos.doMove(undo, m);

            int reduction = 1;
            if (!inCheck && !moveGivesCheck && !isTactical && depth > 2) {
                reduction = LMRTable[std::min(depth, 63)][std::min(movestried, 63)];
                reduction += !inPv;
                reduction -= (m.m == mp.killer1) || (m.m == mp.killer2) || (m.m == mp.counter);
                reduction = std::min(depth - 1, std::max(reduction, 1));
            }

            if (e.doSMP && mp.stage != STG_DEFERRED && depth >= e.defer_depth) e.mht.setBusy(move_hash, depth);
            score = -search(false, false, -alpha - 1, -alpha, depth - reduction, ply + 1, moveGivesCheck);
            if (e.doSMP && mp.stage != STG_DEFERRED && depth >= e.defer_depth) e.mht.resetBusy(move_hash, depth);

            if (reduction > 1 && !e.stop && !stop_iter && score > alpha)
                score = -search(false, false, -alpha - 1, -alpha, depth - 1, ply + 1, moveGivesCheck);

            if (inPv && !e.stop && !stop_iter && score > alpha)
                score = -search(false, inPv, -beta, -alpha, depth - 1, ply + 1, moveGivesCheck);

            pos.undoMove(undo);
        }
        if (e.stop || stop_iter) return 0;

        if (playedmoves[ply].size < 64 && !pos.moveIsTactical(m))
            playedmoves[ply].add(m);

        if (score > best_score) {
            best_score = score;
            if (inRoot) {
                rootmove.m = m.m;
                rootmove.s = best_score;
                updatePV(pvlist, m, ply);
            }
            if (score > alpha) {
                best_move.m = m.m;
                best_move.s = score;
                if (!inRoot) updatePV(pvlist, m, ply);
                if (score >= beta) break;
                alpha = score;
            }
        }
    }
    if (movestried == 0) {
        if (inCheck) return -MATE + ply;
        else return 0;
    }
    if (!inCheck && best_move.m != 0 && !pos.moveIsTactical(best_move)) {
        updateHistory(pos, best_move, depth, ply);
        if (killer1[ply] != best_move.m) {
            killer2[ply] = killer1[ply];
            killer1[ply] = best_move.m;
        }
    }
    best_move.s = scoreToTrans(best_score, ply, MATE - MAXPLY);
    e.tt.store(pos.stack.hash, best_move, depth, (best_score >= beta) ? TT_LOWER : ((inPv && best_move.m != 0) ? TT_EXACT : TT_UPPER));
    return best_score;
}

int search_t::qsearch(bool inPv, int alpha, int beta, int ply, bool inCheck) {
    ASSERT(alpha < beta);
    ASSERT(!(pos.colorBB[WHITE] & pos.colorBB[BLACK]));

    pvlist[ply].size = 0;
    if (stopSearch()) return 0;

    if (ply > maxplysearched) maxplysearched = ply;
    if (pos.stack.fifty > 99 || pos.isRepeat() || pos.isMatDrawn()) return 0;
    if (ply >= MAXPLY) return et.retrieve(pos);

    tt_entry_t tte;
    tte.move.m = 0;
    if (e.tt.retrieve(pos.stack.hash, tte)) {
        tte.move.s = scoreFromTrans(tte.move.s, ply, MATE - MAXPLY);
        if (!inPv && (tte.getBound() == TT_EXACT
            || (tte.getBound() == TT_LOWER && tte.move.s >= beta)
            || (tte.getBound() == TT_UPPER && tte.move.s <= alpha)))
            return tte.move.s;
    }

    int best_score = -MATE;
    if (!inCheck) {
        best_score = et.retrieve(pos);
        if (best_score >= beta) return best_score;
        alpha = std::max(alpha, best_score);
    }

    undo_t undo;
    move_t best_move(0);
    int movestried = 0;
    movepicker_t mp(*this, inCheck, true, std::max(1, alpha - best_score - 100), tte.move.m);
    uint64_t dcc = pos.discoveredPiecesBB(pos.side);
    for (move_t m; mp.getMoves(m);) {
        ++movestried;
        bool moveGivesCheck = pos.moveIsCheck(m, dcc);
        pos.doMove(undo, m);
        int score = -qsearch(inPv, -beta, -alpha, ply + 1, moveGivesCheck);
        pos.undoMove(undo);
        if (e.stop || stop_iter) return 0;
        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                best_move.m = m.m;
                best_move.s = best_score;
                updatePV(pvlist, m, ply);
                if (score >= beta) break;
                alpha = score;
            }
        }
    }
    if (movestried == 0 && inCheck) return -MATE + ply;
    // TODO: store qsearch result in trans table?
    return best_score;
}

void search_t::updateHistory(position_t& p, move_t bm, int depth, int ply) {
    depth = std::min(15, depth);
    int bonus = depth * depth;
    history[p.side][bm.moveFrom()][bm.moveTo()] += bonus;
    auto lm = p.stack.lastmove;
    if (lm.m != 0) countermove[p.side][p.getPiece(lm.moveTo())][lm.moveTo()] = bm.m;
    for (move_t m : playedmoves[ply]) {
        if (m.m == bm.m) continue;
        int& sc = history[p.side][m.moveFrom()][m.moveTo()];
        sc -= bonus / 10;
    }
}