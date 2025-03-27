#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>
#include <iomanip>
#include <atomic>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;
using namespace std::chrono;

// Function to read the template file
std::vector<char> readTemplateFile(const std::string &templatePath)
{
    std::ifstream file(templatePath, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open template file: " + templatePath);
    }

    // Get file size
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read the data
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
    {
        throw std::runtime_error("Failed to read template file");
    }

    return buffer;
}

// File generation function
void generateFiles(const std::vector<char> &templateData,
                   int totalFiles,
                   const std::string &outputDir,
                   double interval_sec,
                   const std::string &filePrefix,
                   int imageCountPerRun,
                   std::atomic<int> &completed)
{

    // Convert time interval to nanoseconds
    auto interval = duration_cast<nanoseconds>(duration<double>(interval_sec));

    // Record start time
    auto startTime = high_resolution_clock::now();
    auto nextTargetTime = startTime;

    for (int i = 0; i < totalFiles; i++)
    {
        // Calculate next scheduled time (previous scheduled time + interval)
        nextTargetTime += interval;

        // Generate output filename with run_number and image_number (1-based indexing)
        // Format: prefix_runNumber_imageNumber (e.g. img_01_00001.tif)
        int runIdx = (i / imageCountPerRun) + 1;      // Current run number (1-based)
        int imageNumber = (i % imageCountPerRun) + 1; // Image number within run (1-based)

        std::stringstream filename;
        filename << outputDir << "/"
                 << filePrefix << "_"
                 << std::setw(2) << std::setfill('0') << runIdx << "_"
                 << std::setw(5) << std::setfill('0') << imageNumber << ".tif";

        // Write template data to the new file
        std::ofstream outFile(filename.str(), std::ios::binary);
        if (outFile)
        {
            outFile.write(templateData.data(), templateData.size());
            outFile.close();

            // Update completion counter
            completed++;

            // Display progress and timing accuracy every 1000 files
            if (completed % 1000 == 0)
            {
                auto currentTime = high_resolution_clock::now();
                auto elapsedTime = duration_cast<milliseconds>(currentTime - startTime).count();
                double expectedTime = completed * interval_sec * 1000;

                std::cout << "Progress: " << completed << " files created"
                          << " (Elapsed: " << elapsedTime << "ms"
                          << ", Expected: " << expectedTime << "ms"
                          << ", Difference: " << (elapsedTime - expectedTime) << "ms)" << std::endl;
            }
        }
        else
        {
            std::cerr << "Error: Could not open file " << filename.str() << std::endl;
        }

        // Wait until next scheduled time
        auto now = high_resolution_clock::now();
        if (now < nextTargetTime)
        {
            std::this_thread::sleep_until(nextTargetTime);
        }
        else
        {
            // Warning if the process can't keep up and already past the next scheduled time
            // Only show every 500 files to avoid excessive messages
            if (i % 500 == 0 && i > 0)
            {
                auto delay = duration_cast<microseconds>(now - nextTargetTime).count();
                std::cerr << "Warning: Process can't keep up. Delayed by " << delay
                          << "us. Consider using faster storage or a longer interval." << std::endl;
            }
        }
    }
}

// Input validation function
template <typename T>
T getValidatedInput(const std::string &prompt, T min_value, T max_value)
{
    T value;
    while (true)
    {
        std::cout << prompt;
        if (std::cin >> value)
        {
            // Clear input buffer
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            // Check value range
            if (value >= min_value && value <= max_value)
            {
                return value;
            }
            else
            {
                std::cout << "Error: Value must be between " << min_value << " and " << max_value << "." << std::endl;
            }
        }
        else
        {
            // Clear invalid input
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Error: Please enter a valid number." << std::endl;
        }
    }
}

// String input function
std::string getStringInput(const std::string &prompt, bool allowEmpty = false)
{
    std::string value;
    while (true)
    {
        std::cout << prompt;
        std::getline(std::cin, value);

        if (allowEmpty || !value.empty())
        {
            return value;
        }
        else
        {
            std::cout << "Error: Empty input is not allowed." << std::endl;
        }
    }
}

int main()
{
    try
    {
        std::cout << "===== TIF File Sequential Generator =====" << std::endl;
        std::cout << "This program generates multiple TIF files at precise intervals" << std::endl;
        std::cout << "using a template TIF file as the source." << std::endl
                  << std::endl;

        // Get user input
        int runNumber = getValidatedInput<int>("Enter the run number [1-10]: ", 1, 10);

        int imageCount = getValidatedInput<int>("Enter the number of images per run [1-18000]: ", 1, 18000);

        double intervalSec = getValidatedInput<double>("Enter the generation interval in seconds [0.001-10.0]: ", 0.001, 10.0);

        std::string templatePath = getStringInput("Enter the path to the template TIF file: ");

        std::string filePrefix = getStringInput("Enter the file prefix [default: img]: ", true);

        // Verify template file exists
        while (!fs::exists(templatePath))
        {
            std::cout << "Error: File not found: " << templatePath << std::endl;
            templatePath = getStringInput("Enter a valid template TIF file path: ");
        }

        std::string outputDir = getStringInput("Enter the output directory [default: tif_output]: ", true);
        if (outputDir.empty())
        {
            outputDir = "tif_output";
        }

        // Create output directory if it doesn't exist
        if (!fs::exists(outputDir))
        {
            try
            {
                fs::create_directories(outputDir);
                std::cout << "Created directory: " << outputDir << std::endl;
            }
            catch (const std::exception &e)
            {
                throw std::runtime_error("Directory creation error: " + std::string(e.what()));
            }
        }

        // Calculate total files across all runs
        int totalFiles = runNumber * imageCount;

        // Confirm parameters
        std::cout << std::endl
                  << "=== Configuration Summary ===" << std::endl;
        std::cout << "Max run number: " << runNumber << " (will generate runs 1 to " << runNumber << ")" << std::endl;
        std::cout << "Images per run: " << imageCount << std::endl;
        std::cout << "Total files to generate: " << totalFiles << std::endl;
        std::cout << "File naming format: " << filePrefix << "_##_#####.tif" << std::endl;
        std::cout << "Example filename: " << filePrefix << "_01_00001.tif" << std::endl;
        std::cout << "Generation interval: " << intervalSec << " seconds" << std::endl;
        std::cout << "Template file: " << templatePath << std::endl;
        std::cout << "Output directory: " << outputDir << std::endl;
        std::cout << "Estimated time: " << (totalFiles * intervalSec) << " seconds" << std::endl;

        std::string confirmation = getStringInput("Start with these settings? (y/n): ");
        if (confirmation != "y" && confirmation != "Y")
        {
            std::cout << "Program terminated." << std::endl;
            return 0;
        }

        // Read the template file
        std::vector<char> templateData = readTemplateFile(templatePath);
        std::cout << "Template file loaded: " << templatePath
                  << " (" << templateData.size() << " bytes)" << std::endl;

        // Record start time
        auto startTime = high_resolution_clock::now();

        // Counter for completed files
        std::atomic<int> completedFiles(0);

        std::cout << std::endl
                  << "=== Generation Started ===" << std::endl;
        std::cout << "Press Ctrl+C to abort." << std::endl;

        // Generate files
        generateFiles(templateData, totalFiles, outputDir, intervalSec, filePrefix, imageCount, completedFiles);

        // Calculate end time and duration
        auto endTime = high_resolution_clock::now();
        auto duration = duration_cast<seconds>(endTime - startTime).count();
        auto idealDuration = totalFiles * intervalSec;

        std::cout << std::endl
                  << "=== Process Completed ===" << std::endl;
        std::cout << "Files generated: " << completedFiles << std::endl;
        std::cout << "Actual time: " << duration << " seconds" << std::endl;
        std::cout << "Ideal time: " << idealDuration << " seconds" << std::endl;
        std::cout << "Time difference: " << (duration - idealDuration) << " seconds" << std::endl;

        if (duration > 0)
        {
            std::cout << "Average generation rate: " << (completedFiles / (double)duration) << " files/second" << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Press any key to exit...";
    std::cin.get();

    return 0;
}