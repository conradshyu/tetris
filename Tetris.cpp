#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace
{
    constexpr int BoardWidth = 10;
    constexpr int BoardHeight = 20;
    constexpr int CellSize = 30;
    constexpr int BoardLeft = 32;
    constexpr int BoardTop = 42;
    constexpr UINT_PTR GameTimer = 1;
    constexpr UINT GameTimerIntervalMs = 16;

    using Shape = std::array<std::array<int, 4>, 4>;

    constexpr Shape Shapes[7] = {
        {{{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
        {{{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
        {{{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
        {{{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
        {{{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
        {{{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
        {{{1, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}}
    };

    constexpr COLORREF PieceColors[7] = {
        RGB(47, 205, 220), RGB(72, 112, 235), RGB(239, 151, 59),
        RGB(239, 207, 63), RGB(80, 196, 104), RGB(178, 89, 205),
        RGB(231, 82, 91)
    };

    struct Piece
    {
        int type = 0;
        int x = 3;
        int y = 0;
        int rotation = 0;
    };

    struct ScoreRecord
    {
        std::uint32_t score = 0;
        std::string initials;
        std::string date;
    };

    enum class GameState
    {
        Playing,
        Paused,
        EnteringInitials,
        GameOver
    };

    std::array<std::array<int, BoardWidth>, BoardHeight> board{};
    std::array<int, 7> pieceBag{};
    int bagPosition = 7;
    std::mt19937 randomEngine{std::random_device{}()};
    Piece currentPiece{};
    int nextPiece = 0;
    std::uint32_t score = 0;
    int clearedLines = 0;
    std::vector<ScoreRecord> highScores;
    std::wstring scoreFilePath;
    std::wstring initials;
    std::wstring statusMessage;
    GameState gameState = GameState::Playing;
    bool downHeld = false;
    ULONGLONG nextFallTime = 0;
    ULONGLONG nextSoftDropTime = 0;
    HFONT titleFont = nullptr;
    HFONT headingFont = nullptr;
    HFONT bodyFont = nullptr;
    HFONT smallFont = nullptr;

    std::array<std::vector<unsigned char>, 3> soundBuffers;

    Shape GetRotatedShape(int type, int rotation)
    {
        Shape result = Shapes[type];
        for (int turn = 0; turn < rotation; ++turn)
        {
            Shape rotated{};
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    rotated[y][x] = result[3 - x][y];
                }
            }
            result = rotated;
        }
        return result;
    }

    int NextPieceType()
    {
        if (bagPosition >= static_cast<int>(pieceBag.size()))
        {
            for (int index = 0; index < static_cast<int>(pieceBag.size()); ++index)
            {
                pieceBag[index] = index;
            }
            std::shuffle(pieceBag.begin(), pieceBag.end(), randomEngine);
            bagPosition = 0;
        }
        return pieceBag[bagPosition++];
    }

    std::wstring GetScoreFilePath()
    {
        wchar_t appData[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return {};
        }
        return (std::filesystem::path(appData) / L"Tetris" / L"highscores.csv").wstring();
    }

    void LoadHighScores()
    {
        scoreFilePath = GetScoreFilePath();
        if (scoreFilePath.empty())
        {
            statusMessage = L"Could not locate the local application data folder.";
            return;
        }

        std::ifstream input{std::filesystem::path(scoreFilePath)};
        if (!input)
        {
            return;
        }

        std::string line;
        while (std::getline(input, line))
        {
            std::istringstream row(line);
            std::string scoreText;
            ScoreRecord record;
            if (std::getline(row, scoreText, ',') &&
                std::getline(row, record.initials, ',') &&
                std::getline(row, record.date) &&
                !scoreText.empty())
            {
                try
                {
                    const unsigned long parsed = std::stoul(scoreText);
                    if (parsed <= std::numeric_limits<std::uint32_t>::max() &&
                        record.initials.size() <= 3)
                    {
                        record.score = static_cast<std::uint32_t>(parsed);
                        highScores.push_back(record);
                    }
                }
                catch (const std::exception&)
                {
                    // Ignore malformed score rows and keep the remaining records.
                }
            }
        }

        std::stable_sort(highScores.begin(), highScores.end(),
            [](const ScoreRecord& left, const ScoreRecord& right)
            {
                return left.score > right.score;
            });
        if (highScores.size() > 10)
        {
            highScores.resize(10);
        }
    }

    void SaveHighScore(const std::string& playerInitials)
    {
        if (scoreFilePath.empty())
        {
            statusMessage = L"Score not saved: the score-file location is unavailable.";
            return;
        }

        SYSTEMTIME now{};
        GetLocalTime(&now);
        char date[16]{};
        sprintf_s(date, sizeof(date), "%04u-%02u-%02u",
            now.wYear, now.wMonth, now.wDay);

        std::vector<ScoreRecord> updatedScores = highScores;
        updatedScores.push_back({score, playerInitials, date});
        std::stable_sort(updatedScores.begin(), updatedScores.end(),
            [](const ScoreRecord& left, const ScoreRecord& right)
            {
                return left.score > right.score;
            });
        if (updatedScores.size() > 10)
        {
            updatedScores.resize(10);
        }

        const std::filesystem::path path(scoreFilePath);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            statusMessage = L"Score not saved: could not create the score folder.";
            return;
        }

        std::ofstream output(path, std::ios::trunc);
        if (!output)
        {
            statusMessage = L"Score not saved: could not write the score file.";
            return;
        }

        for (const ScoreRecord& record : updatedScores)
        {
            output << record.score << ',' << record.initials << ',' << record.date << '\n';
        }
        if (!output)
        {
            statusMessage = L"Score not saved: an error occurred while writing the file.";
            return;
        }
        highScores = std::move(updatedScores);
        statusMessage = L"Score recorded.";
    }

    void AppendWord(std::vector<unsigned char>& output, const char* text)
    {
        output.insert(output.end(), text, text + 4);
    }

    void AppendWord(std::vector<unsigned char>& output, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            output.push_back(static_cast<unsigned char>((value >> shift) & 0xff));
        }
    }

    void AppendShort(std::vector<unsigned char>& output, std::uint16_t value)
    {
        output.push_back(static_cast<unsigned char>(value & 0xff));
        output.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    }

    std::vector<unsigned char> MakeTone(double frequency, int durationMs)
    {
        constexpr std::uint32_t sampleRate = 22050;
        const std::uint32_t sampleCount = sampleRate * durationMs / 1000;
        std::vector<unsigned char> wav;
        wav.reserve(44 + sampleCount * 2);
        AppendWord(wav, "RIFF");
        AppendWord(wav, 36 + sampleCount * 2);
        AppendWord(wav, "WAVE");
        AppendWord(wav, "fmt ");
        AppendWord(wav, 16);
        AppendShort(wav, 1);
        AppendShort(wav, 1);
        AppendWord(wav, sampleRate);
        AppendWord(wav, sampleRate * 2);
        AppendShort(wav, 2);
        AppendShort(wav, 16);
        AppendWord(wav, "data");
        AppendWord(wav, sampleCount * 2);

        for (std::uint32_t sample = 0; sample < sampleCount; ++sample)
        {
            const double fade = std::min(1.0, std::min(sample / 350.0,
                (sampleCount - sample) / 700.0));
            const double phase = 6.283185307179586 * frequency * sample / sampleRate;
            const auto value = static_cast<std::int16_t>(std::sin(phase) * 9500.0 * fade);
            AppendShort(wav, static_cast<std::uint16_t>(value));
        }
        return wav;
    }

    void InitializeSounds()
    {
        soundBuffers[0] = MakeTone(420.0, 65);
        soundBuffers[1] = MakeTone(660.0, 115);
        soundBuffers[2] = MakeTone(180.0, 240);
    }

    void PlayEffect(std::size_t effect)
    {
        if (effect < soundBuffers.size() && !soundBuffers[effect].empty())
        {
            PlaySoundW(reinterpret_cast<LPCWSTR>(soundBuffers[effect].data()),
                nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    }

    int GetGravityIntervalMs()
    {
        const std::uint32_t level = score / 500 + 1;
        return std::max(80, 700 - static_cast<int>(level - 1) * 55);
    }

    bool IsValidPosition(const Piece& piece)
    {
        const Shape shape = GetRotatedShape(piece.type, piece.rotation);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (!shape[y][x])
                {
                    continue;
                }
                const int boardX = piece.x + x;
                const int boardY = piece.y + y;
                if (boardX < 0 || boardX >= BoardWidth || boardY >= BoardHeight)
                {
                    return false;
                }
                if (boardY >= 0 && board[boardY][boardX] != 0)
                {
                    return false;
                }
            }
        }
        return true;
    }

    void BeginInitialsEntry()
    {
        gameState = GameState::EnteringInitials;
        initials.clear();
        downHeld = false;
        PlayEffect(2);
    }

    void SpawnPiece()
    {
        currentPiece = {nextPiece, 3, 0, 0};
        nextPiece = NextPieceType();
        if (!IsValidPosition(currentPiece))
        {
            BeginInitialsEntry();
        }
    }

    void StartNewGame()
    {
        for (auto& row : board)
        {
            row.fill(0);
        }
        score = 0;
        clearedLines = 0;
        initials.clear();
        statusMessage.clear();
        bagPosition = 7;
        nextPiece = NextPieceType();
        gameState = GameState::Playing;
        downHeld = false;
        SpawnPiece();
        const ULONGLONG now = GetTickCount64();
        nextFallTime = now + GetGravityIntervalMs();
        nextSoftDropTime = now;
    }

    void MoveHorizontal(int direction)
    {
        Piece candidate = currentPiece;
        candidate.x += direction;
        if (IsValidPosition(candidate))
        {
            currentPiece = candidate;
        }
    }

    void RotatePiece()
    {
        Piece candidate = currentPiece;
        candidate.rotation = (candidate.rotation + 1) % 4;
        constexpr int wallKicks[] = {0, -1, 1, -2, 2};
        for (int offset : wallKicks)
        {
            candidate.x = currentPiece.x + offset;
            if (IsValidPosition(candidate))
            {
                currentPiece = candidate;
                PlayEffect(0);
                return;
            }
        }
    }

    void LockPiece()
    {
        const Shape shape = GetRotatedShape(currentPiece.type, currentPiece.rotation);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (!shape[y][x])
                {
                    continue;
                }
                const int boardX = currentPiece.x + x;
                const int boardY = currentPiece.y + y;
                if (boardY < 0)
                {
                    BeginInitialsEntry();
                    return;
                }
                if (boardX >= 0 && boardX < BoardWidth &&
                    boardY < BoardHeight)
                {
                    board[boardY][boardX] = currentPiece.type + 1;
                }
            }
        }

        int rowsCleared = 0;
        for (int y = BoardHeight - 1; y >= 0; --y)
        {
            if (std::all_of(board[y].begin(), board[y].end(),
                [](int cell) { return cell != 0; }))
            {
                for (int moveY = y; moveY > 0; --moveY)
                {
                    board[moveY] = board[moveY - 1];
                }
                board[0].fill(0);
                ++rowsCleared;
                ++y;
            }
        }

        if (rowsCleared > 0)
        {
            constexpr std::uint32_t linePoints[] = {0, 100, 300, 500, 800};
            const std::uint32_t level = score / 500 + 1;
            score += linePoints[rowsCleared] * level;
            clearedLines += rowsCleared;
            PlayEffect(1);
        }

        SpawnPiece();
        nextFallTime = GetTickCount64() + GetGravityIntervalMs();
    }

    bool DropOneRow(bool awardPoint)
    {
        Piece candidate = currentPiece;
        ++candidate.y;
        if (IsValidPosition(candidate))
        {
            currentPiece = candidate;
            if (awardPoint)
            {
                ++score;
            }
            return true;
        }
        LockPiece();
        return false;
    }

    void HardDrop()
    {
        int distance = 0;
        while (true)
        {
            Piece candidate = currentPiece;
            ++candidate.y;
            if (!IsValidPosition(candidate))
            {
                break;
            }
            currentPiece = candidate;
            ++distance;
        }
        score += static_cast<std::uint32_t>(distance * 2);
        PlayEffect(0);
        LockPiece();
    }

    void UpdateGame()
    {
        if (gameState != GameState::Playing)
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (downHeld)
        {
            if (now >= nextSoftDropTime)
            {
                DropOneRow(true);
                nextSoftDropTime = now + 45;
            }
        }
        else if (now >= nextFallTime)
        {
            DropOneRow(false);
            nextFallTime = now + GetGravityIntervalMs();
        }
    }

    void DrawTextLine(HDC dc, const wchar_t* text, int x, int y, COLORREF color,
        HFONT font, int width = 0)
    {
        SetTextColor(dc, color);
        HFONT previousFont = static_cast<HFONT>(SelectObject(dc, font));
        RECT area{x, y, width > 0 ? x + width : 1200, y + 32};
        DrawTextW(dc, text, -1, &area, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, previousFont);
    }

    void DrawBlock(HDC dc, int x, int y, int size, COLORREF color)
    {
        RECT block{x + 1, y + 1, x + size - 1, y + size - 1};
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &block, brush);
        DeleteObject(brush);

        const COLORREF highlight = RGB(
            std::min(255, GetRValue(color) + 52),
            std::min(255, GetGValue(color) + 52),
            std::min(255, GetBValue(color) + 52));
        const COLORREF shadow = RGB(
            GetRValue(color) / 2, GetGValue(color) / 2, GetBValue(color) / 2);
        HPEN lightPen = CreatePen(PS_SOLID, 2, highlight);
        HPEN darkPen = CreatePen(PS_SOLID, 2, shadow);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, lightPen));
        MoveToEx(dc, x + 2, y + size - 3, nullptr);
        LineTo(dc, x + 2, y + 2);
        LineTo(dc, x + size - 3, y + 2);
        SelectObject(dc, darkPen);
        MoveToEx(dc, x + size - 3, y + 2, nullptr);
        LineTo(dc, x + size - 3, y + size - 3);
        LineTo(dc, x + 2, y + size - 3);
        SelectObject(dc, oldPen);
        DeleteObject(lightPen);
        DeleteObject(darkPen);
    }

    void DrawMiniPiece(HDC dc, int type, int left, int top, int size)
    {
        const Shape shape = GetRotatedShape(type, 0);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (shape[y][x])
                {
                    DrawBlock(dc, left + x * size, top + y * size,
                        size, PieceColors[type]);
                }
            }
        }
    }

    void DrawGame(HDC dc, RECT client)
    {
        HBRUSH background = CreateSolidBrush(RGB(16, 20, 32));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);

        DrawTextLine(dc, L"BLOCK DROP", BoardLeft, 8, RGB(245, 247, 255), titleFont);

        RECT boardBackground{
            BoardLeft, BoardTop,
            BoardLeft + BoardWidth * CellSize,
            BoardTop + BoardHeight * CellSize
        };
        HBRUSH boardBrush = CreateSolidBrush(RGB(25, 30, 45));
        FillRect(dc, &boardBackground, boardBrush);
        DeleteObject(boardBrush);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(43, 49, 64));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen));
        for (int x = 0; x <= BoardWidth; ++x)
        {
            MoveToEx(dc, BoardLeft + x * CellSize, BoardTop, nullptr);
            LineTo(dc, BoardLeft + x * CellSize, BoardTop + BoardHeight * CellSize);
        }
        for (int y = 0; y <= BoardHeight; ++y)
        {
            MoveToEx(dc, BoardLeft, BoardTop + y * CellSize, nullptr);
            LineTo(dc, BoardLeft + BoardWidth * CellSize, BoardTop + y * CellSize);
        }
        SelectObject(dc, oldPen);
        DeleteObject(gridPen);

        for (int y = 0; y < BoardHeight; ++y)
        {
            for (int x = 0; x < BoardWidth; ++x)
            {
                if (board[y][x] != 0)
                {
                    DrawBlock(dc, BoardLeft + x * CellSize, BoardTop + y * CellSize,
                        CellSize, PieceColors[board[y][x] - 1]);
                }
            }
        }

        if (gameState == GameState::Playing || gameState == GameState::Paused)
        {
            const Shape shape = GetRotatedShape(currentPiece.type, currentPiece.rotation);
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    const int boardX = currentPiece.x + x;
                    const int boardY = currentPiece.y + y;
                    if (shape[y][x] && boardY >= 0 && boardY < BoardHeight &&
                        boardX >= 0 && boardX < BoardWidth)
                    {
                        DrawBlock(dc, BoardLeft + boardX * CellSize,
                            BoardTop + boardY * CellSize, CellSize,
                            PieceColors[currentPiece.type]);
                    }
                }
            }
        }

        const int panelLeft = 375;
        DrawTextLine(dc, L"NEXT", panelLeft, 45, RGB(115, 204, 255), headingFont);
        DrawMiniPiece(dc, nextPiece, panelLeft + 8, 82, 24);

        DrawTextLine(dc, L"SCORE", panelLeft, 220, RGB(115, 204, 255), headingFont);
        wchar_t number[64]{};
        swprintf_s(number, L"%u", score);
        DrawTextLine(dc, number, panelLeft, 250, RGB(245, 247, 255), titleFont);
        DrawTextLine(dc, L"LINES", panelLeft, 300, RGB(115, 204, 255), headingFont);
        swprintf_s(number, L"%d", clearedLines);
        DrawTextLine(dc, number, panelLeft, 328, RGB(245, 247, 255), bodyFont);
        DrawTextLine(dc, L"LEVEL", panelLeft + 125, 300, RGB(115, 204, 255), headingFont);
        swprintf_s(number, L"%u", score / 500 + 1);
        DrawTextLine(dc, number, panelLeft + 125, 328, RGB(245, 247, 255), bodyFont);

        DrawTextLine(dc, L"HIGH SCORES", panelLeft, 380, RGB(115, 204, 255), headingFont);
        for (std::size_t index = 0; index < 5; ++index)
        {
            wchar_t entry[96]{};
            if (index < highScores.size())
            {
                const ScoreRecord& record = highScores[index];
                swprintf_s(entry, L"%2u. %-3S  %7u", static_cast<unsigned>(index + 1),
                    record.initials.c_str(), record.score);
            }
            else
            {
                swprintf_s(entry, L"%2u. ---        ---", static_cast<unsigned>(index + 1));
            }
            DrawTextLine(dc, entry, panelLeft, 412 + static_cast<int>(index) * 24,
                RGB(213, 219, 232), bodyFont);
        }

        DrawTextLine(dc, L"CONTROLS", panelLeft, 550, RGB(115, 204, 255), headingFont);
        DrawTextLine(dc, L"Left / Right   Move", panelLeft, 579, RGB(213, 219, 232), smallFont);
        DrawTextLine(dc, L"Up   Rotate      Down   Soft drop", panelLeft, 601,
            RGB(213, 219, 232), smallFont);
        DrawTextLine(dc, L"Space   Hard drop      P   Pause", panelLeft, 623,
            RGB(213, 219, 232), smallFont);

        if (gameState == GameState::Paused ||
            gameState == GameState::EnteringInitials ||
            gameState == GameState::GameOver)
        {
            RECT overlay{
                BoardLeft + 14, BoardTop + 225,
                BoardLeft + BoardWidth * CellSize - 14, BoardTop + 365
            };
            HBRUSH overlayBrush = CreateSolidBrush(RGB(18, 24, 39));
            FillRect(dc, &overlay, overlayBrush);
            DeleteObject(overlayBrush);
            HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(115, 204, 255));
            oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
            Rectangle(dc, overlay.left, overlay.top, overlay.right, overlay.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(borderPen);

            if (gameState == GameState::Paused)
            {
                DrawTextLine(dc, L"PAUSED", overlay.left + 50, overlay.top + 34,
                    RGB(245, 247, 255), headingFont, 210);
                DrawTextLine(dc, L"Press P to continue", overlay.left + 30,
                    overlay.top + 76, RGB(213, 219, 232), bodyFont, 250);
            }
            else if (gameState == GameState::EnteringInitials)
            {
                DrawTextLine(dc, L"GAME OVER", overlay.left + 36, overlay.top + 20,
                    RGB(255, 133, 133), headingFont, 240);
                DrawTextLine(dc, L"Enter initials (3 letters):", overlay.left + 20,
                    overlay.top + 58, RGB(213, 219, 232), smallFont, 255);
                std::wstring entry = initials + L"_";
                DrawTextLine(dc, entry.c_str(), overlay.left + 20, overlay.top + 82,
                    RGB(245, 247, 255), bodyFont, 250);
                DrawTextLine(dc, L"Press Enter to save; R to restart",
                    overlay.left + 14, overlay.top + 108, RGB(213, 219, 232), smallFont, 260);
            }
            else
            {
                DrawTextLine(dc, L"GAME OVER", overlay.left + 36, overlay.top + 24,
                    RGB(255, 133, 133), headingFont, 240);
                DrawTextLine(dc, L"Game finished", overlay.left + 30,
                    overlay.top + 64, RGB(213, 219, 232), bodyFont, 250);
                DrawTextLine(dc, L"Press R to play again", overlay.left + 22,
                    overlay.top + 98, RGB(213, 219, 232), smallFont, 260);
            }
        }

        if (!statusMessage.empty())
        {
            DrawTextLine(dc, statusMessage.c_str(), panelLeft, 657,
                RGB(255, 202, 114), smallFont, 330);
        }
    }

    void CreateFonts()
    {
        titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        headingFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        bodyFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Consolas");
        smallFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    void DeleteFonts()
    {
        if (titleFont) DeleteObject(titleFont);
        if (headingFont) DeleteObject(headingFont);
        if (bodyFont) DeleteObject(bodyFont);
        if (smallFont) DeleteObject(smallFont);
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            CreateFonts();
            SetTimer(window, GameTimer, GameTimerIntervalMs, nullptr);
            StartNewGame();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_TIMER:
            if (wParam == GameTimer)
            {
                UpdateGame();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_KEYDOWN:
        {
            const bool isRepeat = (lParam & (1LL << 30)) != 0;
            if (wParam == 'R' && !isRepeat)
            {
                StartNewGame();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (gameState == GameState::EnteringInitials)
            {
                if (wParam == VK_RETURN)
                {
                    std::string name;
                    for (wchar_t letter : initials)
                    {
                        name.push_back(static_cast<char>(letter));
                    }
                    if (name.empty())
                    {
                        name = "AAA";
                    }
                    SaveHighScore(name);
                    gameState = GameState::GameOver;
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == 'P' && !isRepeat)
            {
                if (gameState == GameState::Playing)
                {
                    gameState = GameState::Paused;
                    downHeld = false;
                }
                else if (gameState == GameState::Paused)
                {
                    gameState = GameState::Playing;
                    nextFallTime = GetTickCount64() + GetGravityIntervalMs();
                }
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (gameState != GameState::Playing)
            {
                return 0;
            }
            switch (wParam)
            {
            case VK_LEFT:
                MoveHorizontal(-1);
                break;
            case VK_RIGHT:
                MoveHorizontal(1);
                break;
            case VK_UP:
                if (!isRepeat) RotatePiece();
                break;
            case VK_DOWN:
                if (!downHeld)
                {
                    downHeld = true;
                    nextSoftDropTime = GetTickCount64();
                }
                break;
            case VK_SPACE:
                if (!isRepeat) HardDrop();
                break;
            default:
                break;
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }

        case WM_KEYUP:
            if (wParam == VK_DOWN)
            {
                downHeld = false;
                nextFallTime = GetTickCount64() + GetGravityIntervalMs();
                return 0;
            }
            break;

        case WM_CHAR:
            if (gameState == GameState::EnteringInitials)
            {
                if (wParam == VK_BACK)
                {
                    if (!initials.empty())
                    {
                        initials.pop_back();
                    }
                }
                else if ((wParam >= L'a' && wParam <= L'z') ||
                    (wParam >= L'A' && wParam <= L'Z'))
                {
                    if (initials.size() < 3)
                    {
                        initials.push_back(static_cast<wchar_t>(towupper(
                            static_cast<wint_t>(wParam))));
                    }
                }
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_KILLFOCUS:
            downHeld = false;
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC target = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            HDC buffer = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target,
                client.right - client.left, client.bottom - client.top);
            if (buffer && bitmap)
            {
                HBITMAP previousBitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap));
                DrawGame(buffer, client);
                BitBlt(target, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, previousBitmap);
            }
            else
            {
                DrawGame(target, client);
            }
            if (bitmap) DeleteObject(bitmap);
            if (buffer) DeleteDC(buffer);
            EndPaint(window, &paint);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(window, GameTimer);
            DeleteFonts();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    LoadHighScores();
    InitializeSounds();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = L"NativeTetrisWindow";
    if (!RegisterClassExW(&windowClass))
    {
        MessageBoxW(nullptr, L"Could not register the game window.",
            L"Block Drop", MB_OK | MB_ICONERROR);
        return 1;
    }

    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
        WS_MINIMIZEBOX;
    RECT windowRect{0, 0, 720, 700};
    AdjustWindowRect(&windowRect, windowStyle, FALSE);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName,
        L"Block Drop - Native Windows Tetris", windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        nullptr, nullptr, instance, nullptr);
    if (!window)
    {
        MessageBoxW(nullptr, L"Could not create the game window.",
            L"Block Drop", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
