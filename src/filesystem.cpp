/*******************************************************************************

Replacement for some basic functionality available in boost::filesystem
and std::filesystem.

We don't want to use boost::filesystem to avoid introducing a runtime
dependency on boost libraries.

We don't want to use std::filesystem because it is only available in C++17,
and gcc support for C++17 is still limited (particularly with gcc 4.8
which is the version used in CentOs 7).

*******************************************************************************/

// shasta
#include "filesystem.hpp"
#include "SHASTA_ASSERT.hpp"
#include "stdexcept.hpp"
using namespace shasta;

// Linux.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// Standard library.
#include "array.hpp"
#include <filesystem>


// Create a directory. In case of failure, throw an exception.
void shasta::filesystem::createDirectory(const string& path)
{

    if(::mkdir(path.c_str(), 0777) == -1) {
        throw runtime_error("Unable to create directory " + path);
    }
}



// Return the current directory.
string shasta::filesystem::getCurrentDirectory()
{
    const size_t bufferSize = PATH_MAX;
    array<char, bufferSize> buffer;
    ::getcwd(buffer.data(), bufferSize);
    return string(buffer.data());
}



// Change the current directory.
void shasta::filesystem::changeDirectory(const string& path)
{
    if(::chdir(path.c_str()) == -1) {
        throw runtime_error("Unable to change directory to " + path);
    }

}



// Remove the specified path. In case of failure, throw an exception.
void shasta::filesystem::remove(const string& path)
{
    if(::unlink(path.c_str()) == -1) {
        throw runtime_error("Unable to remove " + path);
    }
}



// Move (rename). In case of failure, throw an exception.
void shasta::filesystem::move(const string& oldPath, const string& newPath)
{
    if(not std::filesystem::exists(oldPath)) {
        throw runtime_error("Unable to rename/move " + oldPath + " to " + newPath +
            " because " + oldPath + " does not exist.");

    }
    if(std::filesystem::exists(newPath)) {
        throw runtime_error("Unable to rename/move " + oldPath + " to " + newPath +
            " because " + newPath + " already exists.");

    }
    if(::rename(oldPath.c_str(), newPath.c_str()) != 0) {
        throw runtime_error("Unable to rename/move " + oldPath + " to " + newPath);
    }
}



// Copy a file.
void shasta::filesystem::copy(const string& input, const string& output)
{
    const string command = "cp " + input + " " + output;
    if(::system(command.c_str()) != 0) {
        throw runtime_error("Error executing command " + command);
    }
}



// Return the contents of a directory. In case of failure, throw an exception.
vector<string> shasta::filesystem::directoryContents(const string& path)
{
    DIR* dir = opendir(path.c_str());
    if(!dir) {
        throw runtime_error("Error listing contents of directory " + path);
    }

    vector<string> directoryContents;
    ::dirent* entry = 0;
    while(true) {
        entry = ::readdir(dir);
        if(!entry) {
            break;
        }
        const string name(entry->d_name);
        if(name!="." && name!="..") {
            directoryContents.push_back(path + "/" + name);
        }
    }

    closedir(dir);
    return directoryContents;
}



// Return the extension of a path - that is, everything following
// the last dot after the last slash.
// If there is no dot after the last slash, throw an exception.
string shasta::filesystem::extension(const string& path)
{
    // If the path is empty, throw an exception.
    if(path.empty()) {
        throw runtime_error("Cannot extract extension of empty path.");
    }

    // Loop backward beginning at the end.
    size_t i = path.size()-1;
    while(true) {
        const char c = path[i];

        // If we find a slash before a dot (looping from the end), there is no extension.
        if(c == '/') {
            throw runtime_error("Cannot extract extension from  path " + path);
        }

        // If we find a dot, return everything that follows it.
        if(c == '.') {
            return path.substr(i+1);
        }

        // If we reached the beginning of the string, there is no extension.
        if(i==0) {
            throw runtime_error("Cannot extract extension from  path " + path);
        }

        // Check the previous character.
        --i;
    }
}



// Return everything up to the last dot following the last dash of a path.
// If there is no dot following the last dash, throw an exception.
string shasta::filesystem::fileName(const string& path)
{
    // If the path is empty, throw an exception.
    if(path.empty()) {
        throw runtime_error("Cannot extract file name of empty path.");
    }

    // Loop backward beginning at the end.
    size_t i = path.size()-1;
    while(true) {
        const char c = path[i];

        // If we find a slash before a dot (looping from the end), there is no extension.
        if(c == '/') {
            throw runtime_error("Cannot extract file name from  path " + path);
        }

        // If we find a dot, return everything that precedes it.
        if(c == '.') {
            return path.substr(0, i);
        }

        // If we reached the beginning of the string, there is no extension.
        if(i==0) {
            throw runtime_error("Cannot extract file name from  path " + path);
        }

        // Check the previous character.
        --i;
    }
}



// Find the absolute path.
string shasta::filesystem::getAbsolutePath(const string& path)
{
    const size_t bufferSize = PATH_MAX;
    array<char, bufferSize> buffer;
    ::realpath(path.c_str(), buffer.data());
    return string(buffer.data());
}


string shasta::filesystem::executablePath() {
    string path;
    vector<char> buf(PATH_MAX, 0);

    size_t bufSize = buf.size();
    ssize_t bytesRead = readlink("/proc/self/exe", &buf[0], bufSize);
    if (bytesRead < 0) {
        throw runtime_error("Could not read path of executable.");
    }
    path = string(&buf[0], bytesRead);
    return path;
}

