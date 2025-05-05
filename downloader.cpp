#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <sstream>
#include <algorithm>  // For std::min and std::max

using namespace std;
using namespace chrono;

struct ChunkData {
    string filename;
    FILE* file;
    size_t start;
    size_t end;
    size_t size;
    double downloaded;
    int id;
    time_point<steady_clock> start_time;
    time_point<steady_clock> last_update;
};

vector<ChunkData> chunks;
mutex progress_mutex;

const string COLOR_RESET = "\033[0m";
const string ERASE_LINE = "\033[2K";
const string BOLD = "\033[1m";
const string CURSOR_SAVE = "\033[s";
const string CURSOR_RESTORE = "\033[u";

// Convert file size to human-readable format
string format_size(double size_bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    
    while (size_bytes >= 1024 && unit_index < 4) {
        size_bytes /= 1024;
        unit_index++;
    }
    
    stringstream ss;
    ss << fixed << setprecision(2) << size_bytes << " " << units[unit_index];
    return ss.str();
}

// Convert seconds to human-readable format
string format_time(double seconds) {
    if (seconds < 60) {
        return to_string(static_cast<int>(seconds)) + "s";
    } else if (seconds < 3600) {
        int mins = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        return to_string(mins) + "m " + to_string(secs) + "s";
    } else {
        int hours = static_cast<int>(seconds) / 3600;
        int mins = (static_cast<int>(seconds) % 3600) / 60;
        int secs = static_cast<int>(seconds) % 60;
        return to_string(hours) + "h " + to_string(mins) + "m " + to_string(secs) + "s";
    }
}

// Create progress bar string
string create_progress_bar(double percentage, int width = 30) {
    int pos = width * percentage;
    string bar = "[";
    
    for (int i = 0; i < width; ++i) {
        if (i < pos) bar += "=";
        else if (i == pos) bar += ">";
        else bar += " ";
    }
    
    bar += "] " + to_string(static_cast<int>(percentage * 100)) + "%";
    return bar;
}

string get_color_gradient(double progress) {
    int r = static_cast<int>(255 * (1 - progress));
    int g = static_cast<int>(255 * progress);
    int b = 50;  // Add a bit of blue for better visibility
    return "\033[38;2;" + to_string(r) + ";" + to_string(g) + ";" + to_string(b) + "m";
}

void move_cursor_to_line(int line) {
    cout << "\033[" << line << ";0H";
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ChunkData* chunk = static_cast<ChunkData*>(userp);
    return fwrite(contents, size, nmemb, chunk->file);
}

static int progress_callback(void* clientp, curl_off_t, curl_off_t dlnow, curl_off_t, curl_off_t) {
    ChunkData* chunk = static_cast<ChunkData*>(clientp);
    auto now = steady_clock::now();

    {
        lock_guard<mutex> lock(progress_mutex);
        chunk->downloaded = static_cast<double>(dlnow);
        chunk->last_update = now;

        // Total progress
        double total_downloaded = 0, total_size = 0;
        for (const auto& c : chunks) {
            total_downloaded += c.downloaded;
            total_size += c.size;
        }

        double progress = total_size > 0 ? total_downloaded / total_size : 0;
        double elapsed = duration_cast<seconds>(now - chunks[0].start_time).count();
        double speed = elapsed > 0 ? total_downloaded / elapsed : 0;
        double eta = speed > 0 ? (total_size - total_downloaded) / speed : 0;

        // Save cursor and go to top
        cout << CURSOR_SAVE;
        move_cursor_to_line(1);
        
        // Display total progress at the top
        cout << ERASE_LINE << BOLD << "Total Progress: " << COLOR_RESET << endl;
        cout << ERASE_LINE;
        
        string color = get_color_gradient(progress);
        cout << color << create_progress_bar(progress, 50) << COLOR_RESET << endl;
        
        cout << ERASE_LINE << "Downloaded: " << format_size(total_downloaded) 
             << " of " << format_size(total_size)
             << " (" << format_size(speed) << "/s)" << endl;
        
        cout << ERASE_LINE << "Elapsed: " << format_time(elapsed) 
             << " - ETA: " << format_time(eta) << endl;
        
        cout << ERASE_LINE << "-------------------------------------------------------------------------" << endl;
        
        // Chunk-specific progress
        int line = chunk->id + 6;  // +6 to account for the header lines
        move_cursor_to_line(line);
        cout << ERASE_LINE;

        double chunk_progress = chunk->size > 0 ? chunk->downloaded / chunk->size : 0;
        string chunk_color = get_color_gradient(chunk_progress);

        cout << "Chunk " << setw(2) << chunk->id << ": " 
             << chunk_color << create_progress_bar(chunk_progress, 30) << COLOR_RESET 
             << " " << format_size(chunk->downloaded) << "/" << format_size(chunk->size);

        // Restore cursor position
        cout << CURSOR_RESTORE;
        cout.flush();
    }

    return 0;
}

curl_off_t get_file_size(const string& url) {
    curl_off_t file_size = -1;
    CURL* curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        if (curl_easy_perform(curl) == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &file_size);
        }
        curl_easy_cleanup(curl);
    }

    return file_size;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <URL> [num_chunks]" << endl;
        return 1;
    }

    string url = argv[1];
    
    // Optional parameter for number of chunks
    size_t user_defined_chunks = 0;
    if (argc > 2) {
        user_defined_chunks = strtoul(argv[2], nullptr, 10);
    }
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl_off_t file_size = get_file_size(url);

    if (file_size <= 0) {
        cerr << "Failed to determine file size or file is empty." << endl;
        curl_global_cleanup();
        return 1;
    }

    string filename = url.substr(url.find_last_of("/") + 1);
    if (filename.empty() || filename.find('?') != string::npos)
        filename = "downloaded_file";

    // Calculate optimal number of chunks based on file size
    const size_t chunk_size = 8 * 1024 * 1024;  // 8MB per chunk is a reasonable default
    size_t num_chunks;
    
    if (user_defined_chunks > 0) {
        num_chunks = user_defined_chunks;
    } else {
        // Convert to same type before using min
        size_t chunks_by_size = static_cast<size_t>((file_size + chunk_size - 1) / chunk_size);
        num_chunks = min(chunks_by_size, static_cast<size_t>(32));
        // Ensure at least 2 chunks for small files if possible
        num_chunks = max(static_cast<size_t>(2), min(num_chunks, static_cast<size_t>(file_size / 1024)));
    }
    
    // Ensure at least 1 chunk
    num_chunks = max(static_cast<size_t>(1), num_chunks);

    cout << "File size: " << format_size(file_size) << endl;
    cout << "Using " << num_chunks << " connection" << (num_chunks > 1 ? "s" : "") << endl;
    cout << "Target file: " << filename << endl;
    
    chunks.resize(num_chunks);
    auto global_start = steady_clock::now();

    for (size_t i = 0; i < num_chunks; ++i) {
        chunks[i].id = i + 1;
        // Calculate chunk boundaries
        chunks[i].start = i * (file_size / num_chunks);
        chunks[i].end = (i == num_chunks - 1) ? file_size - 1 : (i + 1) * (file_size / num_chunks) - 1;
        chunks[i].size = chunks[i].end - chunks[i].start + 1;
        chunks[i].downloaded = 0;
        chunks[i].filename = filename + ".part" + to_string(i + 1);
        chunks[i].file = fopen(chunks[i].filename.c_str(), "wb");
        chunks[i].start_time = global_start;
        chunks[i].last_update = global_start;

        if (!chunks[i].file) {
            cerr << "Failed to open chunk file: " << chunks[i].filename << endl;
            // Clean up already opened files
            for (size_t j = 0; j < i; ++j) {
                fclose(chunks[j].file);
                remove(chunks[j].filename.c_str());
            }
            curl_global_cleanup();
            return 1;
        }
    }

    // Clear screen and prepare display area
    cout << "\033[2J\033[H";  // Clear screen and move cursor to home position
    
    // Print header placeholders
    for (size_t i = 0; i < 6 + num_chunks + 2; ++i) {
        cout << endl;
    }

    CURLM* multi_handle = curl_multi_init();
    vector<CURL*> easy_handles(num_chunks);

    for (size_t i = 0; i < num_chunks; ++i) {
        CURL* eh = curl_easy_init();
        easy_handles[i] = eh;

        string range = to_string(chunks[i].start) + "-" + to_string(chunks[i].end);

        curl_easy_setopt(eh, CURLOPT_URL, url.c_str());
        curl_easy_setopt(eh, CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(eh, CURLOPT_WRITEDATA, &chunks[i]);
        curl_easy_setopt(eh, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(eh, CURLOPT_XFERINFODATA, &chunks[i]);
        curl_easy_setopt(eh, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(eh, CURLOPT_PRIVATE, &chunks[i]);
        curl_easy_setopt(eh, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(eh, CURLOPT_LOW_SPEED_TIME, 30L);  // Timeout after 30s of low speed

        curl_multi_add_handle(multi_handle, eh);
    }

    int still_running = 1;
    while (still_running) {
        curl_multi_perform(multi_handle, &still_running);
        
        // Use select to avoid busy-waiting
        int numfds;
        const int max_wait_msecs = 100;
        curl_multi_wait(multi_handle, nullptr, 0, max_wait_msecs, &numfds);
        
        // Check for completed transfers
        CURLMsg* msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy_handle = msg->easy_handle;
                CURLcode result = msg->data.result;
                
                // Find which chunk this handle belongs to
                ChunkData* chunk_ptr = nullptr;
                curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &chunk_ptr);
                
                if (chunk_ptr && result != CURLE_OK) {
                    lock_guard<mutex> lock(progress_mutex);
                    cout << "\nError downloading chunk " << chunk_ptr->id 
                         << ": " << curl_easy_strerror(result) << endl;
                }
                
                curl_multi_remove_handle(multi_handle, easy_handle);
            }
        }
    }

    // Close all chunk files
    for (auto& chunk : chunks) {
        fclose(chunk.file);
    }

    // Clean up CURL handles
    for (auto h : easy_handles) {
        curl_easy_cleanup(h);
    }
    curl_multi_cleanup(multi_handle);

    // Move cursor to below the progress display
    move_cursor_to_line(6 + num_chunks + 2);
    cout << "Merging chunks into final file..." << endl;

    // Merge files
    ofstream output(filename, ios::binary);
    if (!output.is_open()) {
        cerr << "Failed to create output file: " << filename << endl;
        // Clean up chunk files
        for (const auto& chunk : chunks) {
            remove(chunk.filename.c_str());
        }
        curl_global_cleanup();
        return 1;
    }

    for (const auto& chunk : chunks) {
        ifstream input(chunk.filename, ios::binary);
        if (input.is_open()) {
            output << input.rdbuf();
            input.close();
            remove(chunk.filename.c_str());
        } else {
            cerr << "Warning: Failed to open chunk file for merging: " << chunk.filename << endl;
        }
    }

    output.close();
    auto global_end = steady_clock::now();
    auto total_time = duration_cast<seconds>(global_end - global_start).count();

    cout << BOLD << "Download complete in " << format_time(total_time) 
         << ". Saved as: " << filename << COLOR_RESET << endl;
    cout << "Final file size: " << format_size(file_size) << endl;

    curl_global_cleanup();
    return 0;
}
