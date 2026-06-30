#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstring>

using namespace std;

typedef uint64_t U64;
enum Piece { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NONE };
enum Color { WHITE, BLACK };

#define INF 9999999
#define MATE_SCORE 990000

struct TTEntry {
    U64 key; int depth; int flag; int score;
};
unordered_map<U64, TTEntry> transpositionTable;

U64 zobristPieces[2][6][64];
U64 zobristSide;

void initZobrist() {
    U64 randState = 1804289383ULL;
    auto shiftRand = [&]() {
        randState ^= randState << 13; randState ^= randState >> 7; randState ^= randState << 17;
        return randState;
    };
    for(int c=0; c<2; ++c)
        for(int p=0; p<6; ++p)
            for(int s=0; s<64; ++s) zobristPieces[c][p][s] = shiftRand();
    zobristSide = shiftRand();
}

const int PIECE_VALUES[] = { 100, 325, 330, 500, 900, 100000 };

// Gelişmiş Konumsal Matris (Merkez Kontrolü ve Tuzak Kareleri)
const int CENTER_PST[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-10,  0,  5,  5,  0,-10,-20,
    -10,  0, 10, 20, 20, 10,  0,-10,
     -5,  5, 15, 30, 30, 15,  5, -5,
     -5,  5, 15, 30, 30, 15,  5, -5,
    -10,  5, 10, 20, 20, 10,  5,-10,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -30,-40,-40,-50,-50,-40,-40,-30
};

struct Move {
    int from; int to; int piece; int captured; int score;
    bool operator<(const Move& other) const { return score > other.score; }
};

// Killer Moves (Aramayı hızlandıran katil hamleler)
Move killerMoves[2][64]; 

class Board {
public:
    U64 pieces[2][6];
    Color sideToMove;
    U64 hashKey;

    Board() { reset(); }

    void reset() {
        for(int c=0; c<2; ++c) for(int p=0; p<6; ++p) pieces[c][p] = 0ULL;
        sideToMove = WHITE; hashKey = 0ULL;
        parseFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    }

    void parseFEN(const string& fen) {
        int r = 7, f = 0;
        for (char c : fen) {
            if (c == ' ') break;
            if (c == '/') { r--; f = 0; }
            else if (isdigit(c)) { f += c - '0'; }
            else {
                int color = isupper(c) ? WHITE : BLACK;
                int type = NONE; char p = tolower(c);
                if (p == 'p') type = PAWN; else if (p == 'n') type = KNIGHT;
                else if (p == 'b') type = BISHOP; else if (p == 'r') type = ROOK;
                else if (p == 'q') type = QUEEN; else if (p == 'k') type = KING;

                if (type != NONE) {
                    pieces[color][type] |= (1ULL << (r * 8 + f));
                    hashKey ^= zobristPieces[color][type][r * 8 + f];
                }
                f++;
            }
        }
    }

    // Taktiksel Tuzak kurma ve Konum Değerlendirme Fonksiyonu
    int evaluate() {
        int score = 0;
        U64 whiteAll = 0, blackAll = 0;
        for(int p=0; p<6; ++p) { whiteAll |= pieces[WHITE][p]; blackAll |= pieces[BLACK][p]; }

        for (int p = 0; p < 6; ++p) {
            U64 wBits = pieces[WHITE][p];
            U64 bBits = pieces[BLACK][p];
            while (wBits) {
                int sq = __builtin_ctzll(wBits);
                score += PIECE_VALUES[p] + CENTER_PST[sq];
                // Şah Güvenliği Tehdit Kontrolü (Tuzak algısı)
                if (p == QUEEN && (pieces[BLACK][KING] & 0x7000000000000000ULL)) score += 50; 
                wBits &= wBits - 1;
            }
            while (bBits) {
                int sq = __builtin_ctzll(bBits);
                score -= (PIECE_VALUES[p] + CENTER_PST[sq ^ 56]);
                if (p == QUEEN && (pieces[WHITE][KING] & 0x0000000000000070ULL)) score -= 50;
                bBits &= bBits - 1;
            }
        }
        return (sideToMove == WHITE) ? score : -score;
    }
};

void generateMoves(Board& board, vector<Move>& moveList) {
    int us = board.sideToMove; int them = !us;
    U64 myPieces = 0, enemyPieces = 0;
    for(int p=0; p<6; ++p) { myPieces |= board.pieces[us][p]; enemyPieces |= board.pieces[them][p]; }
    U64 allPieces = myPieces | enemyPieces;

    // Piyade Hamle Kombinasyonları
    U64 pawns = board.pieces[us][PAWN];
    while (pawns) {
        int from = __builtin_ctzll(pawns);
        int to = from + (us == WHITE ? 8 : -8);
        if (to >= 0 && to < 64 && !(allPieces & (1ULL << to))) {
            moveList.push_back({from, to, PAWN, NONE, 0});
            int doubleTo = from + (us == WHITE ? 16 : -16);
            if (((us == WHITE && from / 8 == 1) || (us == BLACK && from / 8 == 6)) && !(allPieces & (1ULL << doubleTo))) {
                moveList.push_back({from, doubleTo, PAWN, NONE, 10}); // Tuzak açılışları için agresif puanlama
            }
        }
        int targets[] = {from + (us == WHITE ? 7 : -9), from + (us == WHITE ? 9 : -7)};
        for(int toTarget : targets) {
            if (toTarget >= 0 && toTarget < 64 && abs((toTarget % 8) - (from % 8)) == 1) {
                if (enemyPieces & (1ULL << toTarget)) moveList.push_back({from, toTarget, PAWN, QUEEN, 50});
            }
        }
        pawns &= pawns - 1;
    }

    // At ve Hafif Taş Hamleleri
    U64 knights = board.pieces[us][KNIGHT];
    while (knights) {
        int from = __builtin_ctzll(knights);
        int offsets[] = {-17, -15, -10, -6, 6, 10, 15, 17};
        for (int offset : offsets) {
            int to = from + offset;
            if (to >= 0 && to < 64 && abs((to % 8) - (from % 8)) <= 2) {
                if (!(myPieces & (1ULL << to))) {
                    int cap = (enemyPieces & (1ULL << to)) ? QUEEN : NONE;
                    moveList.push_back({from, to, KNIGHT, cap, 0});
                }
            }
        }
        knights &= knights - 1;
    }
}

bool makeMove(Board& board, const Move& m) {
    int us = board.sideToMove;
    board.pieces[us][m.piece] &= ~(1ULL << m.from);
    board.pieces[us][m.piece] |= (1ULL << m.to);
    board.hashKey ^= zobristPieces[us][m.piece][m.from] ^ zobristPieces[us][m.piece][m.to];

    if (m.captured != NONE) {
        for(int p=0; p<6; ++p) {
            if (board.pieces[!us][p] & (1ULL << m.to)) {
                board.pieces[!us][p] &= ~(1ULL << m.to);
                board.hashKey ^= zobristPieces[!us][p][m.to];
                break;
            }
        }
    }
    if (board.pieces[us][KING] == 0) return false;
    board.sideToMove = (Color)!us;
    board.hashKey ^= zobristSide;
    return true;
}

void unmakeMove(Board& board, const Move& m) {
    int us = !board.sideToMove;
    board.pieces[us][m.piece] &= ~(1ULL << m.to);
    board.pieces[us][m.piece] |= (1ULL << m.from);
    board.hashKey ^= zobristPieces[us][m.piece][m.from] ^ zobristPieces[us][m.piece][m.to];

    if (m.captured != NONE) {
        board.pieces[!us][PAWN] |= (1ULL << m.to);
        board.hashKey ^= zobristPieces[!us][PAWN][m.to];
    }
    board.sideToMove = (Color)us;
    board.hashKey ^= zobristSide;
}

// --- Negamax Alpha-Beta with Null Move Pruning & Advanced Tactical Logic ---
int negamax(Board& board, int depth, int alpha, int beta, Move& bestMove, bool allowNull = true) {
    if (transpositionTable.count(board.hashKey)) {
        TTEntry entry = transpositionTable[board.hashKey];
        if (entry.depth >= depth) {
            if (entry.flag == 0) return entry.score;
            if (entry.flag == 1 && entry.score <= alpha) return alpha;
            if (entry.flag == 2 && entry.score >= beta) return beta;
        }
    }

    if (depth == 0) return board.evaluate();

    // 1. Null Move Pruning (NMP) - İleri Düzey Derinlik Kesmesi
    if (allowNull && depth >= 3 && board.evaluate() >= beta) {
        board.sideToMove = (Color)!board.sideToMove;
        board.hashKey ^= zobristSide;
        Move dummy;
        int nullScore = -negamax(board, depth - 1 - 2, -beta, -beta + 1, dummy, false);
        board.sideToMove = (Color)!board.sideToMove;
        board.hashKey ^= zobristSide;
        if (nullScore >= beta) return beta; // Tehdit yoksa derin aramayı geç
    }

    vector<Move> moveList;
    generateMoves(board, moveList);

    // Hamle Sıralaması (Tuzaklar ve Katil Hamleler Öncelikli)
    for (auto& m : moveList) {
        if (m.captured != NONE) m.score = 2000 + PIECE_VALUES[m.captured];
        else if (killerMoves[0][depth].from == m.from && killerMoves[0][depth].to == m.to) m.score = 1500;
        else m.score = board.evaluate();
    }
    sort(moveList.begin(), moveList.end());

    int bestScore = -INF;
    Move localBestMove; int origAlpha = alpha;
    int legalMovesCount = 0;

    for (const auto& m : moveList) {
        if (!makeMove(board, m)) continue;
        legalMovesCount++;
        
        Move dummy;
        int score = -negamax(board, depth - 1, -beta, -alpha, dummy);
        unmakeMove(board, m);

        if (score > bestScore) {
            bestScore = score; localBestMove = m;
        }
        alpha = max(alpha, score);
        if (alpha >= beta) {
            // Beta Cut-off tetiklendiğinde Katil Hamleyi kaydet
            if (m.captured == NONE) {
                killerMoves[1][depth] = killerMoves[0][depth];
                killerMoves[0][depth] = m;
            }
            break;
        }
    }

    if (legalMovesCount == 0) return -MATE_SCORE + depth; // Mat veya Pat

    bestMove = localBestMove;
    
    TTEntry entry = {board.hashKey, depth, (bestScore <= origAlpha) ? 1 : ((bestScore >= beta) ? 2 : 0), bestScore};
    transpositionTable[board.hashKey] = entry;

    return bestScore;
}

void uciLoop() {
    Board board; string line; initZobrist();
    while (getline(cin, line)) {
        if (line == "uci") {
            cout << "id name HexaEngine Pro v3.0\nid author Bartu\nuciok" << endl;
        } else if (line == "isready") {
            cout << "readyok" << endl;
        } else if (line.rfind("go", 0) == 0) {
            Move bestMove = {0, 0, NONE, NONE, 0};
            // Iterative Deepening ile derin arama optimizasyonu
            for (int d = 1; d <= 8; ++d) {
                negamax(board, d, -INF, INF, bestMove);
            }
            char fCol = 'a' + (bestMove.from % 8), fRow = '1' + (bestMove.from / 8);
            char tCol = 'a' + (bestMove.to % 8), tRow = '1' + (bestMove.to / 8);
            cout << "bestmove " << fCol << fRow << tCol << tRow << endl;
        } else if (line == "quit") break;
    }
}

int main() { uciLoop(); return 0; }