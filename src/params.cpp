/**************************************************/
/*  Invictus 2019                                 */
/*  Edsel Apostol                                 */
/*  ed_apostol@yahoo.com                          */
/**************************************************/

#include "params.h"
#include "attacks.h"
#include "log.h"
#include "constants.h"

using namespace Attacks;

namespace EvalParam {
    score_t pawnPST(int sq) {
        score_t file[8] = { { -3, 3 },{ -1, 1 },{ 0, -1 },{ 3, -3 },{ 3, -3 },{ 0, -1 },{ -1, 1 },{ -3, 3 } };
        score_t rank[8] = { { 0, 0 },{ 0, -20 },{ 0, -10 },{ 1, 0 },{ 1 ,5 },{ 0, 10 },{ 0, 15 },{ 0, 0 } };
        return file[sqFile(sq)] + rank[sqRank(sq)];
    }
    score_t knightPST(int sq) {
        score_t file[8] = { { -15, -7 },{ -8, -3 },{ 8, 3 },{ 15, 7 },{ 15, 7 },{ 8, 3 },{ -8, -3 },{ -15, -7 } };
        score_t rank[8] = { { -30, -10 },{ -14, -4 },{ -2, -1 },{ 8, 3 },{ 16, 7 },{ 22, 12 },{ 11, 3 },{ -11, -10 } };
        return file[sqFile(sq)] + rank[sqRank(sq)];
    }
    score_t bishopPST(int sq) {
        score_t rank[8] = { { -4, -5 },{ 0, -1 },{ 0, 1 },{ 4, 5 },{ 4, 5 },{ 0, 1 },{ 0, -1 },{ -4, -5 } };
        score_t file[8] = { { -4, -5 },{ 0, -1 },{ 0, 1 },{ 4, 5 },{ 4, 5 },{ 0, 1 },{ 0, -1 },{ -4, -5 } };
        return file[sqFile(sq)] + rank[sqRank(sq)];
    }
    score_t rookPST(int sq) {
        score_t file[8] = { { -4, 0 },{ -1, 0 },{ 1, 0 },{ 4, 0 },{ 4, 0 },{ 1, 0 },{ -1, 0 },{ -4, 0 } };
        return file[sqFile(sq)];
    }
    score_t queenPST(int sq) {
        score_t file[8] = { { -3, -3 },{ -1, -1 },{ 1, 1 },{ 3, 3 },{ 3, 3 },{ 1, 1 },{ -1, -1 },{ -3, -3 } };
        score_t rank[8] = { { -4, -5 },{ 0, 0 },{ 1, 1 },{ 2, 3 },{ 2, 3 },{ 1, 1 },{ 1, 0 },{ -3, -3 } };
        return file[sqFile(sq)] + rank[sqRank(sq)];
    }
    score_t kingPST(int sq) {
        score_t file[8] = { { 26, -13 },{ 30, 1 },{ 0, 11 },{ -20, 16 },{ -20, 16 },{ 0, 11 },{ 30, 1 },{ 26, -13 } };
        score_t rank[8] = { {20, -29 },{ -5, -4 },{ -25, 1 },{ -29, 6 },{ -33, 10 },{ -37, 6 },{ -37, 1 },{ -37, -10 } };
        return file[sqFile(sq)] + rank[sqRank(sq)];
    }

    score_t MaterialValues[7] = { { 0, 0 },{ 100, 113 },{ 335, 433 },{ 351, 436 },{ 453, 765 },{ 996, 1523 },{ 0, 0 } };
    score_t BishopPair = { 15, 69 };

    score_t PawnConnected = { 0, 14 };
    score_t PawnDoubled = { 9, 0 };
    score_t PawnIsolated = { 14, 14 };
    score_t PawnBackward = { 6, 0 };

    score_t PasserBonusMin = { 0, 0 };
    score_t PasserBonusMax = { 27, 117 };
    score_t PasserDistOwn = { 0, 23 };
    score_t PasserDistEnemy = { 0, 41 };
    score_t PasserNotBlocked = { 0, 71 };
    score_t PasserSafePush = { 0, 23 };
    score_t PasserSafeProm = { 0, 166 };

    score_t KnightMob = { 6, 6 };
    score_t BishopMob = { 7, 6 };
    score_t RookMob = { 3, 6 };
    score_t QueenMob = { 3, 5 };
    score_t RookOn7th = { 0, 54 };
    score_t RookOnSemiOpenFile = { 20, 10 };
    score_t RookOnOpenFile = { 43, 13 };
    basic_score_t Tempo = 34;

    score_t WeakPawns = { 2, 56 };
    score_t PawnsxMinors = { 64, 17 };
    score_t MinorsxMinors = { 28, 43 };
    score_t MajorsxWeakMinors = { 27, 72 };
    score_t PawnsMinorsxMajors = { 26, 33 };
    score_t AllxQueens = { 44, 35 };

    score_t ShelterBonus = { 3, 13 };

    basic_score_t KnightAtk = 8;
    basic_score_t BishopAtk = 5;
    basic_score_t RookAtk = 13;
    basic_score_t QueenAtk = 3;
    basic_score_t AttackValue = 19;
    basic_score_t WeakSquares = 47;
    basic_score_t NoEnemyQueens = 92;
    basic_score_t EnemyPawns = 25;
    basic_score_t QueenSafeCheckValue = 56;
    basic_score_t RookSafeCheckValue = 113;
    basic_score_t BishopSafeCheckValue = 67;
    basic_score_t KnightSafeCheckValue = 114;

    score_t PcSqTab[2][8][64];
    uint64_t KingZoneBB[2][64];
    uint64_t KingShelterBB[2][3];
    uint64_t KingShelter2BB[2][3];

    void initArr() {
        std::function<score_t(int)> pstInit[] = { pawnPST, knightPST,bishopPST,rookPST,queenPST,kingPST };
        memset(PcSqTab, 0, sizeof(PcSqTab));
        for (int pc = PAWN; pc <= KING; ++pc) {
            score_t total;
            for (int sq = 0; sq < 64; ++sq) {
                int rsq = ((7 - sqRank(sq)) * 8) + sqFile(sq);
                PcSqTab[WHITE][pc][sq] = pstInit[pc - 1](sq);
                PcSqTab[BLACK][pc][sq] = pstInit[pc - 1](rsq);
                //total += PcSqTab[WHITE][pc][sq];
            }
            //PrintOutput() << "pc: " << pc << " Mid: " << total.m << " End: " << total.e;
        }
        for (int sq = 0; sq < 64; ++sq) {
            for (int color = WHITE; color <= BLACK; ++color) {
                KingZoneBB[color][sq] = kingMovesBB(sq) | (1ull << sq) | shiftBB[color](kingMovesBB(sq), 8);
                KingZoneBB[color][sq] |= sqFile(sq) == FileA ? KingZoneBB[color][sq] << 1 : 0;
                KingZoneBB[color][sq] |= sqFile(sq) == FileH ? KingZoneBB[color][sq] >> 1 : 0;
            }
        }
        const int KingSquare[2][3] = { {B1, E1, G1}, {B8, E8, G8} };
        for (int color = WHITE; color <= BLACK; ++color) {
            for (int castle = 0; castle <= 2; ++castle) {
                KingShelterBB[color][castle] = kingMovesBB(KingSquare[color][castle]) & Rank2ByColorBB[color];
                KingShelter2BB[color][castle] = shiftBB[color](KingShelterBB[color][castle], 8);
            }
        }
    }
    void displayPSTbyPC(score_t A[], std::string piece, bool midgame) {
        LogAndPrintOutput() << piece << ":";
        for (int r = 56; r >= 0; r -= 8) {
            LogAndPrintOutput logger;
            for (int f = 0; f <= 7; ++f) {
                logger << (midgame ? A[r + f].m : A[r + f].e) << " ";
            }
        }
        LogAndPrintOutput() << "\n\n";
    }
    void displayPST() {
        static const std::string colstr[2] = { "WHITE", "BLACK" };
        static const std::string pcstr[7] = { "EMPTY", "PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING" };
        for (int c = 0; c <= 1; ++c) {
            for (int pc = 1; pc <= 6; ++pc) {
                LogAndPrintOutput() << "MIDGAME";
                displayPSTbyPC(PcSqTab[c][pc], colstr[c] + " " + pcstr[pc], true);
                LogAndPrintOutput() << "ENDGAME";
                displayPSTbyPC(PcSqTab[c][pc], colstr[c] + " " + pcstr[pc], false);
            }
        }
    }
}