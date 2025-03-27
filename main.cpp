#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <memory>
#include <cstring>
#include <set>
#include <map>
#include <algorithm>
#include <regex>
#include <snappy.h>
#include <queue>
#include <condition_variable>

// ファイルシステム名前空間のエイリアス
namespace fs = std::filesystem;

// スレッドセーフなログ出力用
std::mutex cout_mutex;
#define LOG(msg)                                      \
    {                                                 \
        std::lock_guard<std::mutex> lock(cout_mutex); \
        std::cout << msg << std::endl;                \
    }

// 削除キュークラス - ファイル削除をバックグラウンドで処理
class DeleteQueue
{
private:
    struct DeleteTask
    {
        std::set<std::string> files;
        std::string firstFile; // 削除しないファイル
    };

    std::queue<DeleteTask> tasks;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::thread worker_thread;
    bool running;

    // ワーカースレッド関数
    void worker()
    {
        while (running)
        {
            DeleteTask task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                if (tasks.empty())
                {
                    // キューが空の場合は通知を待つ（1秒のタイムアウト付き）
                    cv.wait_for(lock, std::chrono::seconds(1), [this]
                                { return !tasks.empty() || !running; });
                    if (!running && tasks.empty())
                        break;
                    if (tasks.empty())
                        continue;
                }

                task = tasks.front();
                tasks.pop();
            }

            // タスクを処理（ファイル削除）
            for (const auto &filePath : task.files)
            {
                // 最初のファイルは削除しない
                if (filePath == task.firstFile)
                {
                    LOG("Keeping first file of set: " << fs::path(filePath).filename().string());
                    continue;
                }

                try
                {
                    fs::remove(filePath);
                }
                catch (const std::exception &e)
                {
                    LOG("Error removing file " << filePath << ": " << e.what());
                }
            }
        }
    }

public:
    DeleteQueue() : running(true)
    {
        worker_thread = std::thread(&DeleteQueue::worker, this);
    }

    ~DeleteQueue()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            running = false;
        }
        cv.notify_all();
        if (worker_thread.joinable())
        {
            worker_thread.join();
        }
    }

    void push(const std::set<std::string> &files, const std::string &firstFile)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            DeleteTask task;
            task.files = files;
            task.firstFile = firstFile;
            tasks.push(task);
        }
        cv.notify_one();
    }

    void push(const std::set<std::string> &files)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            DeleteTask task;
            task.files = files;
            tasks.push(task);
        }
        cv.notify_one();
    }

    // キュー内のタスク数を取得
    size_t size()
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return tasks.size();
    }
};

// グローバル削除キューインスタンス
std::unique_ptr<DeleteQueue> deleteQueue;

// 512バイトぴったりの構造体となるようにパック
#pragma pack(push, 1)
struct TarHeader
{
    char name[100];     // ファイル名
    char mode[8];       // ファイルモード（8進数文字列）
    char uid[8];        // オーナーのuid（8進数文字列）
    char gid[8];        // オーナーのgid（8進数文字列）
    char size[12];      // ファイルサイズ（8進数文字列）
    char mtime[12];     // 修正時刻（8進数文字列）
    char checksum[8];   // チェックサム（8進数文字列）
    char typeflag;      // タイプフラグ ('0'または'\0'は通常のファイル、'5'はディレクトリ)
    char linkname[100]; // リンク先
    char magic[6];      // ターベックの識別子 ("ustar")
    char version[2];    // バージョン
    char uname[32];     // オーナー名
    char gname[32];     // グループ名
    char devmajor[8];   // デバイスメジャー番号（8進数文字列）
    char devminor[8];   // デバイスマイナー番号（8進数文字列）
    char prefix[155];   // パスのプレフィックス
    char padding[12];   // パディング（合計512バイトにするため）
};
#pragma pack(pop)

// 8進数フォーマットで数値を文字列に変換
void octalNumber(char *dest, size_t size, long value)
{
    std::snprintf(dest, size, "%0*lo", static_cast<int>(size - 1), value);
}

// カスタムTARアーカイブ作成クラス
class CustomTarCreator
{
private:
    std::vector<char> buffer;

public:
    CustomTarCreator()
    {
        // 初期バッファサイズを確保
        buffer.reserve(1024 * 1024); // 1MB初期サイズ
    }

    bool addFile(const std::string &filepath)
    {
        std::ifstream file(filepath, std::ios::binary);
        if (!file)
        {
            LOG("Error opening file: " << filepath);
            return false;
        }

        // ファイルサイズを取得
        file.seekg(0, std::ios::end);
        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // ファイルデータを読み込む
        std::vector<char> fileData(fileSize);
        if (!file.read(fileData.data(), fileSize))
        {
            LOG("Error reading file: " << filepath);
            return false;
        }

        // TARヘッダーを準備
        TarHeader header;
        std::memset(&header, 0, sizeof(TarHeader));

        // ファイル名設定（パスは除外してファイル名のみ）
        std::string filename = fs::path(filepath).filename().string();
        std::strncpy(header.name, filename.c_str(), sizeof(header.name) - 1);

        // パーミッション（644 = 読み書き+読み取り専用）
        std::strcpy(header.mode, "000644 ");

        // 所有者とグループID
        std::strcpy(header.uid, "000000 ");
        std::strcpy(header.gid, "000000 ");

        // ファイルサイズ（8進数）
        octalNumber(header.size, sizeof(header.size), fileSize);

        // 最終更新時間（現在時刻）
        std::time_t now = std::time(nullptr);
        octalNumber(header.mtime, sizeof(header.mtime), now);

        // ファイルタイプ（通常ファイル）
        header.typeflag = '0';

        // UStar形式指定
        std::strcpy(header.magic, "ustar");
        std::strcpy(header.version, "00");

        // 所有者名とグループ名
        std::strcpy(header.uname, "user");
        std::strcpy(header.gname, "group");

        // チェックサム計算 - 仮にスペースで埋める
        std::memset(header.checksum, ' ', sizeof(header.checksum));

        // ヘッダー全体の合計を計算
        unsigned int sum = 0;
        const unsigned char *data = reinterpret_cast<const unsigned char *>(&header);
        for (size_t i = 0; i < sizeof(TarHeader); ++i)
        {
            sum += data[i];
        }

        // 8進数フォーマットでチェックサム設定
        std::snprintf(header.checksum, sizeof(header.checksum), "%06o", sum);
        header.checksum[6] = '\0';
        header.checksum[7] = ' ';

        // ヘッダーを追加
        size_t currentSize = buffer.size();
        buffer.resize(currentSize + sizeof(TarHeader));
        std::memcpy(buffer.data() + currentSize, &header, sizeof(TarHeader));

        // ファイルデータを追加
        currentSize = buffer.size();
        buffer.resize(currentSize + fileData.size());
        std::memcpy(buffer.data() + currentSize, fileData.data(), fileData.size());

        // ブロックサイズ（512バイト）に合わせてパディング
        size_t paddingSize = (512 - (fileData.size() % 512)) % 512;
        if (paddingSize > 0)
        {
            currentSize = buffer.size();
            buffer.resize(currentSize + paddingSize, 0);
        }

        return true;
    }

    void finalize()
    {
        // TARファイルの終端（1024バイトのゼロブロック）
        size_t currentSize = buffer.size();
        buffer.resize(currentSize + 1024, 0);
    }

    std::vector<char> getBuffer()
    {
        finalize();
        return buffer;
    }
};

// ファイルセットをグループ化するための関数
struct FileSet
{
    int run;
    int setNumber;               // setNumberはファイルセットの先頭番号
    std::set<std::string> files; // セット内のファイルパス
    std::string firstFile;       // 最初のファイル（パターン基準）

    // 出力ファイル名の生成
    std::string getOutputPath(const std::string &outputDir) const
    {
        // 最初のファイルのパスからファイル名を取得
        fs::path firstFilePath(firstFile);
        std::string filename = firstFilePath.stem().string(); // 拡張子を除いたファイル名
        return outputDir + "/" + filename + ".snappy";
    }
};

// ディレクトリをスキャンし、パターンに合致するファイルをセットとしてグループ化
std::vector<FileSet> scanAndGroupFiles(const std::string &dir, const std::string &basePattern, int setSize)
{
    std::map<std::pair<int, int>, FileSet> fileSets; // (run, setNumber) -> FileSet

    // 正規表現パターン作成（例："test_(\d\d)_(\d\d\d\d\d)\.tif"）
    std::regex filePattern(basePattern.substr(0, basePattern.find("_##_")) +
                           "_([0-9]{2})_([0-9]{5})\\.tif");

    LOG("Scanning directory: " << dir);

    try
    {
        // ディレクトリを走査
        for (const auto &entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;

            std::string filename = entry.path().filename().string();
            std::smatch matches;

            if (std::regex_match(filename, matches, filePattern) && matches.size() >= 3)
            {
                int run = std::stoi(matches[1].str());
                int fileNumber = std::stoi(matches[2].str());
                int setNumber = ((fileNumber - 1) / setSize) * setSize + 1; // セットの先頭番号を計算

                auto key = std::make_pair(run, setNumber);
                if (fileSets.find(key) == fileSets.end())
                {
                    // 新しいセットを作成
                    FileSet newSet;
                    newSet.run = run;
                    newSet.setNumber = setNumber;
                    fileSets[key] = newSet;
                }

                // ファイルをセットに追加
                fileSets[key].files.insert(entry.path().string());

                // セット内の最初のファイルを記録
                if (fileNumber == setNumber)
                {
                    fileSets[key].firstFile = entry.path().string();
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG("Error scanning directory: " << e.what());
    }

    // マップからベクターに変換
    std::vector<FileSet> result;
    for (const auto &pair : fileSets)
    {
        result.push_back(pair.second);
    }

    // セット番号でソート
    std::sort(result.begin(), result.end(), [](const FileSet &a, const FileSet &b)
              {
        if (a.run != b.run) return a.run < b.run;
        return a.setNumber < b.setNumber; });

    return result;
}

// セットが完全であるか確認（ファイル数がsetSize個あるか）
bool isSetComplete(const FileSet &fileSet, int setSize)
{
    return fileSet.files.size() >= setSize;
}

// 既に処理済みのセットか確認（出力ファイルが存在するか）
bool isSetProcessed(const FileSet &fileSet, const std::string &outputDir)
{
    std::string outputPath = fileSet.getOutputPath(outputDir);
    return fs::exists(outputPath);
}

// ファイルセットを処理する関数
bool processFileSet(const FileSet &fileSet, const std::string &outputDir, bool deleteAfter = true)
{
    try
    {
        // 処理開始時間を記録
        auto startTime = std::chrono::high_resolution_clock::now();

        std::string outputPath = fileSet.getOutputPath(outputDir);

        // 既に処理済みならスキップ
        if (fs::exists(outputPath))
        {
            LOG("Skipping already processed set: " << outputPath);
            return true;
        }

        LOG("Processing file set: run " << fileSet.run << ", set " << fileSet.setNumber << " with " << fileSet.files.size() << " files");

        // メモリ上でTARを作成
        CustomTarCreator tarCreator;
        for (const auto &filePath : fileSet.files)
        {
            if (!tarCreator.addFile(filePath))
            {
                LOG("Failed to add file to tar: " << filePath);
                continue;
            }
        }

        std::vector<char> tarBuffer = tarCreator.getBuffer();

        // Snappyで圧縮
        std::string compressedData;
        snappy::Compress(tarBuffer.data(), tarBuffer.size(), &compressedData);

        // 出力ディレクトリが存在しない場合は作成
        fs::create_directories(fs::path(outputPath).parent_path());

        // 圧縮データを保存
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
        {
            LOG("Error opening output file: " << outputPath);
            return false;
        }
        outFile.write(compressedData.data(), compressedData.size());
        outFile.close();

        // 先頭ファイルを出力ディレクトリにコピー
        if (!fileSet.firstFile.empty())
        {
            fs::path firstFilePath(fileSet.firstFile);
            fs::path destPath = fs::path(outputDir) / firstFilePath.filename();

            try
            {
                // ファイルが存在している場合は上書き
                if (fs::exists(destPath))
                {
                    fs::remove(destPath);
                }
                fs::copy_file(firstFilePath, destPath, fs::copy_options::overwrite_existing);
                LOG("Copied first file to output directory: " << destPath.filename().string());
            }
            catch (const std::exception &e)
            {
                LOG("Error copying first file: " << e.what());
            }
        }

        // 元ファイルを削除 - 削除キューに追加
        if (deleteAfter)
        {
            // 削除タスクを削除キューに追加（すべてのファイルを削除）
            deleteQueue->push(fileSet.files);
        }

        // 処理終了時間と経過時間を計算
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        LOG("Created: " << fs::path(outputPath).filename().string() << " - Processing time: " << duration << " ms");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG("Error processing file set: " << e.what());
        return false;
    }
}

// メインの監視ループ
void monitorDirectory(const std::string &watchDir, const std::string &outputDir,
                      const std::string &basePattern, int setSize, int pollInterval,
                      int maxThreads, bool deleteAfter, bool stopOnInterrupt)
{

    bool running = true;

    LOG("Starting directory monitor on: " << watchDir);
    LOG("Output directory: " << outputDir);
    LOG("Set size: " << setSize << " files");
    LOG("Poll interval: " << pollInterval << " seconds");
    LOG("Max threads: " << maxThreads);

    // 削除キューを初期化
    deleteQueue = std::make_unique<DeleteQueue>();

    // 出力ディレクトリがなければ作成
    try
    {
        fs::create_directories(outputDir);
    }
    catch (const std::exception &e)
    {
        LOG("Error creating output directory: " << e.what());
        return;
    }

    // スレッドプール
    std::vector<std::thread> threads;

    // Ctrl+C 処理
    if (stopOnInterrupt)
    {
        std::thread interruptThread([&running]()
                                    {
            LOG("Press Enter to stop the monitor...");
            std::cin.get();
            running = false;
            LOG("Stopping monitor..."); });
        interruptThread.detach();
    }

    // メインループ
    while (running)
    {
        try
        {
            // 処理済みセットを追跡
            static std::set<std::pair<int, int>> processedSets;
            // 不完全セット（まだ揃っていないセット）を追跡
            static std::set<std::pair<int, int>> incompleteSetsSeen;

            // ディレクトリをスキャンしてファイルセットを取得
            auto fileSets = scanAndGroupFiles(watchDir, basePattern, setSize);

            LOG("Found " << fileSets.size() << " file sets");

            // 各セットを処理
            for (const auto &fileSet : fileSets)
            {
                auto setKey = std::make_pair(fileSet.run, fileSet.setNumber);

                // 既に処理済みならスキップ
                if (processedSets.find(setKey) != processedSets.end())
                {
                    continue;
                }

                // セットが完全であるか確認
                if (isSetComplete(fileSet, setSize))
                {
                    // 既に処理済みかチェック
                    if (isSetProcessed(fileSet, outputDir))
                    {
                        LOG("Set already processed: run " << fileSet.run << ", set " << fileSet.setNumber);
                        processedSets.insert(setKey);
                        continue;
                    }

                    // 不完全セットリストから削除（もし以前に不完全として記録されていたなら）
                    incompleteSetsSeen.erase(setKey);

                    // スレッド数をチェック
                    while (threads.size() >= static_cast<size_t>(maxThreads))
                    {
                        // 完了したスレッドを削除
                        for (auto it = threads.begin(); it != threads.end();)
                        {
                            if (it->joinable())
                            {
                                it->join();
                                it = threads.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }

                    // 新しいスレッドで処理
                    LOG("Starting processing of set: run " << fileSet.run << ", set " << fileSet.setNumber);
                    threads.emplace_back(processFileSet, fileSet, outputDir, deleteAfter);
                    processedSets.insert(setKey);
                }
                else
                {
                    // 不完全なセットを記録
                    incompleteSetsSeen.insert(setKey);
                    LOG("Set incomplete: run " << fileSet.run << ", set " << fileSet.setNumber << " (" << fileSet.files.size() << "/" << setSize << " files)");
                }
            }

            // 完了したスレッドをクリーンアップ
            for (auto it = threads.begin(); it != threads.end();)
            {
                if (it->joinable())
                {
                    it->join();
                    it = threads.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        catch (const std::exception &e)
        {
            LOG("Error in monitor loop: " << e.what());
        }

        // キューの状態をログに出力（オプション）
        LOG("Delete queue size: " << deleteQueue->size());

        // 指定された間隔で待機
        for (int i = 0; i < pollInterval && running; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // 残りのスレッドが完了するのを待つ
    LOG("Waiting for remaining tasks to complete...");
    for (auto &thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    // 削除キューを解放
    LOG("Waiting for delete queue to finish...");
    deleteQueue.reset();

    LOG("Monitor stopped.");
}

int main(int argc, char *argv[])
{
    // Default settings
    std::string watchDir = "Z:";
    std::string outputDir = "Z:";
    std::string basePattern = "test_##_#####.tif";
    int setSize = 100;
    int pollInterval = 1;               // Poll interval in seconds
    const int maxThreads = 4;           // Maximum number of threads (fixed)
    const bool deleteAfter = true;      // Always delete source files after processing
    const bool stopOnInterrupt = false; // Never stop on Enter key

    // Get user input
    std::cout << "=== Snappy Composer Settings ===" << std::endl;

    // Watch directory input
    std::cout << "Enter directory to monitor: ";
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty())
    {
        watchDir = input;
    }

    // Output directory input
    std::cout << "Enter directory for output files: ";
    input.clear();
    std::getline(std::cin, input);
    if (!input.empty())
    {
        outputDir = input;
    }

    // File pattern input
    std::cout << "Enter filename pattern: ";
    input.clear();
    std::getline(std::cin, input);
    if (!input.empty())
    {
        basePattern = input;
    }

    // Set size input
    std::cout << "Enter number of files per set: ";
    input.clear();
    std::getline(std::cin, input);
    if (!input.empty())
    {
        try
        {
            setSize = std::stoi(input);
        }
        catch (const std::exception &e)
        {
            std::cout << "Invalid input. Using default value: " << setSize << std::endl;
        }
    }

    std::cout << "\n=== Monitor Configuration ===" << std::endl;
    std::cout << "Watch directory: " << watchDir << std::endl;
    std::cout << "Output directory: " << outputDir << std::endl;
    std::cout << "File pattern: " << basePattern << std::endl;
    std::cout << "Set size: " << setSize << std::endl;
    std::cout << "\nStarting monitor...\n"
              << std::endl;

    try
    {
        monitorDirectory(watchDir, outputDir, basePattern, setSize, pollInterval, maxThreads, deleteAfter, stopOnInterrupt);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}