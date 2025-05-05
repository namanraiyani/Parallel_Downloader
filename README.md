# Parallel_Downloader

Multi-threaded file downloading tool that uses concurrent connections to significantly increase download speeds. It features real-time progress bars, intelligent formatting, and robust error handling.

![ezgif-3a024d43e8e1fc](https://github.com/user-attachments/assets/2cbebbff-b288-44d1-8f28-e4225e664497)


## Features

- **Parallel downloading**: Uses multiple connections to accelerate downloads
- **Real-time progress tracking**: Visual progress bars for both individual chunks and total progress
- **Intelligent formatting**: Automatically formats file sizes and time in appropriate units
- **Color-coded progress**: Progress bars change from red to green as downloads complete
- **Automatic resumption**: Download chunks independently - if one connection fails, others continue
- **Configurable**: Choose how many parallel connections to use

## How It Works

1. Determining the size of the file to download
2. Splitting the file into multiple chunks
3. Creating a separate connection for each chunk
4. Downloading chunks in parallel
5. Displaying progress in real-time with color-coded indicators
6. Merging chunks into a single file upon completion

## Requirements

- C++11 compatible compiler (GCC, Clang)
- libcurl development package
- ANSI-compatible terminal

## Installation

### Install Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential libcurl4-openssl-dev
```

### Compile the Program

```bash
g++ -o downloader downloader.cpp -lcurl -std=c++11 -pthread
```

## Usage

```bash
./downloader <URL> [num_chunks]
```

### Parameters:

- `<URL>`: Direct URL to the file you want to download
- `[num_chunks]`: (Optional) Number of parallel connections to use

### Examples:

Download with default number of connections (automatically calculated based on file size):
```bash
./downloader https://example.com/large-file.zip
```

Download with 8 parallel connections:
```bash
./downloader https://speed.hetzner.de/100MB.bin 8
```

## Performance Notes

- Larger files benefit more from multiple connections
- Some servers limit the number of parallel connections from a single IP
- Performance depends on your network connection and the server's capabilities
- For optimal performance, experiment with different chunk counts

## Error Handling

- If a chunk download fails, an error message is displayed
- The program attempts to recover when possible
- In case of critical errors, partial downloads are cleaned up
