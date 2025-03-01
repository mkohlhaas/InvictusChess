/**************************************************/
/*  Invictus 2021                                 */
/*  Edsel Apostol                                 */
/*  ed_apostol@yahoo.com                          */
/**************************************************/

#include <algorithm>
#include <numeric>
#include "typedefs.h"
#include "position.h"
#include "utils.h"
#include "bitutils.h"
#include "eval.h"
#include "log.h"
#include "movepicker.h"
#include "engine.h"

engine_t::engine_t() {
    initUCIoptions();
    mht.init(2); // 2Mb
    onHashChange();
    onThreadsChange();
}

engine_t::~engine_t() {
    while (!empty()) delete back(), pop_back();
}

void engine_t::initSearch() {
    start_time = Utils::getTime();

    int mytime = 0, t_inc = 0;
    if (limits.infinite)
        limits.depth = MAXPLY;
    if (limits.mate)
        limits.depth = limits.mate * 2 - 1;

    if (limits.movetime) {
        time_limit_max = limits.movetime;
        time_limit_abs = limits.movetime;
    }
    if (origpos.side == WHITE) {
        mytime = limits.wtime;
        t_inc = limits.winc;
    }
    else {
        mytime = limits.btime;
        t_inc = limits.binc;
    }
    if (mytime || t_inc) {
        mytime = std::max(0, mytime - 1000);
        if (limits.movestogo < 1 || limits.movestogo > 30) limits.movestogo = 30;

        time_limit_max = (mytime / limits.movestogo) + ((t_inc * 4) / 5);
        if (limits.ponder) time_limit_max += time_limit_max / 4;
        if (time_limit_max > mytime) time_limit_max = mytime;

        time_limit_abs = ((mytime * 3) / 10) + ((t_inc * 4) / 5);
        if (time_limit_abs < time_limit_max) time_limit_abs = time_limit_max;
        if (time_limit_abs > mytime) time_limit_abs = mytime;
    }
    if (!limits.depth) limits.depth = MAXPLY;
    LogInfo() << "max time = " << time_limit_max << " abs time = " << time_limit_abs << " depth = " << limits.depth;

    tt.updateAge();

    time_range = time_limit_max;
    time_limit_max += start_time;
    time_limit_abs += start_time;
    use_time = !limits.ponder && (mytime || limits.movetime);
    stop = false;
    resolve_iter = false;
    rootbestmove.m = 0;
    rdepth = 1;
    alpha = -MATE;
    beta = -MATE;

    defer_depth = options["ABDADA Depth"].getIntVal();
    cutoffcheck_depth = options["Cutoff Check Depth"].getIntVal();
    doNUMA = options["NUMA"].getIntVal();

    if (doSMP = size() > 1) {
        mht.clear();
    }

    for (auto& a : plysearched) a = false;

    for (auto t : *this) {
        t->pos = origpos;
        t->wakeup();
    }
}

void engine_t::waitForThreads() {
    for (auto t : *this) {
        while (!t->do_sleep)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void engine_t::ponderhit() {
    use_time = true;
}

void engine_t::onHashChange() {
    tt.init(options["Hash"].getIntVal());
}

void engine_t::onThreadsChange() {
    size_t threads = options["Threads"].getIntVal();
    while (size() < threads) {
        int id = (int)size();
        emplace_back(new search_t(id, *this));
    }
    while (size() > threads) delete back(), pop_back();
}

void engine_t::newgame() {
    tt.resetAge();
    tt.clear();
    for (auto t : *this) t->et.clear();
}

void engine_t::stopthreads() {
    stop = true;
}

void engine_t::stopIteration() {
    for (auto &t : *this) t->stop_iter = true;
}

void engine_t::initUCIoptions() {
    options["Hash"] = uci_options_t(256, 1, 65536, [&] { onHashChange(); });
    options["Threads"] = uci_options_t(8, 1, 4096, [&] { onThreadsChange(); });
    options["Ponder"] = uci_options_t(false, [&] {});
    options["ABDADA Depth"] = uci_options_t(3, 1, 128, [&] {});
    options["Cutoff Check Depth"] = uci_options_t(4, 1, 128, [&] {});
    options["NUMA"] = uci_options_t(false, [&] {});
}

void engine_t::printUCIoptions() {
    options.print();
}

uint64_t engine_t::nodesearched() {
    uint64_t nodes = 0;
    for (auto t : *this) nodes += t->nodecnt;
    return nodes;
}