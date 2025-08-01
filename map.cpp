/*  A program to create a PNG map from region files
    Copyright (C) 2025 Anonymous1212144

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "colours.h"
#include "format.h"
#include "zlib.h"

const int infos[13][2] = {{0, 0}, {1, 0}, {2, 0}, {4, 0}, {8, 0}, {4, 0}, {8, 0}, {2, 1}, {2, 0}, {5, 0}, {0, 0}, {2, 4}, {2, 8}};
const std::unordered_set<std::string> interesting = {"Heightmaps", "Name", "Properties", "WORLD_SURFACE", "Y", "age", "axis", "block_states", "data", "half", "open", "palette", "part", "sections", "type", "waterlogged"};
const std::unordered_set<std::string> prop_types = {"age", "axis", "half", "open", "part", "type", "waterlogged"};

uint64_t heightmap[37];
bool map_set;
std::vector<uint8_t> palette[25];
std::vector<uint64_t> blocks[25];
bool blocks_set[25] = {};
std::vector<std::vector<uint8_t>> output;

const uint8_t *ptr;
int prop_temp = 0;
std::string name_temp;
std::vector<uint64_t> blocks_temp;
bool b_temp_set = false;
std::vector<uint8_t> palette_temp;
uint8_t y = 0;
std::unordered_set<std::string> invalids;

inline int check_water(const std::string &name, const int prop)
{
    // Check whether a block counts as water and/or displays as water
    if (name == "water")
        return 3;
    if (!(prop & 1))
        return 0;
    if (name == "scaffolding")
        return 2;
    if (name.ends_with("_slab") || name.ends_with("_stairs") || name.ends_with("_trapdoor"))
    {
        if (prop & 2)
            return 3;
        return 2 | ((prop >> 4) & 1);
    }
    return 3;
}

inline uint8_t process_name(const std::string &name, const int prop)
{
    // Convert block name to bytes representing its colour
    auto pair = COLOURS.find(name);
    if (pair == COLOURS.end() && !invalids.contains(name))
    {
        std::cerr << "\nCannot find colour data for \"" << name << "\", defaulting to void\n";
        invalids.insert(name);
        return 0;
    }
    uint16_t colour = pair->second;
    if (name.ends_with("_log"))
    {
        if (prop & 4)
            return colour & 255;
        else
            return colour >> 8;
    }
    else if (name.ends_with("_bed"))
    {
        if (prop & 8)
            return colour & 255;
        else
            return colour >> 8;
    }
    else if (name == "wheat")
    {
        if (prop & 32)
            return colour & 255;
        else
            return colour >> 8;
    }
    else
    {
        return (check_water(name, prop) << 6) | (colour & 255);
    }
}

inline std::array<int, 3> get_state(const uint8_t d)
{
    // Get the next state given the byte
    std::array<int, 3> result;
    std::copy_n(infos[d], 2, result.begin() + 1);
    switch (d)
    {
    case 8:
        result[0] = 1;
        break;
    case 9:
        result[0] = 4;
        break;
    case 10:
        result[0] = 5;
        break;
    default:
        if (d < 7)
            result[0] = 2;
        else
            result[0] = 3;
        break;
    }
    return result;
}

void parse_none(uint32_t length, const std::array<int, 3> &state_info)
{
    // Stripped parse for when the section is uninteresting
    int next_state = 0;
    int state;
    std::array<uint8_t, 2> info;
    bool sinfo_set;
    if (state_info[0])
    {
        state = state_info[0];
        std::copy_n(state_info.begin() + 1, 2, info.begin());
        sinfo_set = true;
    }
    else
    {
        state = 0;
        sinfo_set = false;
    }
    while (length--)
    {
        switch (state)
        {
        case 0:
        {
            if (!(*ptr))
            {
                ptr++;
                return;
            }
            std::array<int, 3> sinfo = get_state(*ptr);
            ptr++;
            next_state = sinfo[0];
            std::copy_n(sinfo.begin() + 1, 2, info.begin());
            state = 1;
            break;
        }
        case 1:
        {
            uint16_t n;
            std::memcpy(&n, ptr, 2);
            n = std::byteswap(n);
            ptr += 2;
            if (!sinfo_set)
                state = next_state;
            if (next_state == 1)
                next_state = 0;
            ptr += n;
            break;
        }
        case 2:
            if (!sinfo_set)
                state = 0;
            ptr += info[0];
            break;
        case 3:
        {
            uint32_t n;
            std::memcpy(&n, ptr, 4);
            n = std::byteswap(n);
            ptr += 4 + n * info[1];
            if (!sinfo_set)
                state = 0;
            break;
        }
        case 4:
        {
            std::array<int, 3> sinfo = get_state(*ptr);
            ptr++;
            uint32_t n;
            std::memcpy(&n, ptr, 4);
            n = std::byteswap(n);
            ptr += 4;
            parse_none(n, sinfo);
            if (!sinfo_set)
                state = 0;
            break;
        }
        case 5:
            parse_none(-1, {0, 0, 0});
            if (!sinfo_set)
                state = 0;
            break;
        }
    }
}

void parse(uint32_t length, const std::array<int, 3> &state_info, std::string name)
{
    // mca file parser, but only focuses on sections that are relevant to maps
    int next_state = 0;
    int state;
    std::array<uint8_t, 2> info;
    bool sinfo_set;
    if (state_info[0])
    {
        state = state_info[0];
        std::copy_n(state_info.begin() + 1, 2, info.begin());
        sinfo_set = true;
    }
    else
    {
        state = 0;
        sinfo_set = false;
    }
    while (length--)
    {
        switch (state)
        {
        case 0:
        {
            if (!(*ptr))
            {
                ptr++;
                return;
            }
            std::array<int, 3> sinfo = get_state(*ptr);
            ptr++;
            next_state = sinfo[0];
            std::copy_n(sinfo.begin() + 1, 2, info.begin());
            state = 1;
            break;
        }
        case 1:
        {
            uint16_t n;
            std::memcpy(&n, ptr, 2);
            n = std::byteswap(n);
            ptr += 2;
            if (!sinfo_set)
                state = next_state;
            if (next_state)
                name.assign(reinterpret_cast<const char *>(ptr), n);
            else
            {
                if (interesting.contains(name))
                {
                    std::string item(reinterpret_cast<const char *>(ptr), n);
                    if (name == "Name")
                    {
                        name_temp.resize(n - 10);
                        name_temp = item.substr(10);
                    }
                    else if (prop_types.contains(name))
                    {
                        uint16_t n2;
                        std::memcpy(&n2, &name[0], 2);
                        switch (n2)
                        {
                        case 24951:
                            if (item == "true")
                                prop_temp |= 1;
                            break;
                        case 30817:
                            if (item == "y")
                                prop_temp |= 4;
                            break;
                        case 24944:
                            if (item == "head")
                                prop_temp |= 8;
                            break;
                        case 28783:
                            if (item == "true")
                                prop_temp |= 16;
                            break;
                        case 26465:
                            if (std::stoi(item) >= 6)
                                prop_temp |= 32;
                            break;
                        default:
                            if (item == "bottom")
                                prop_temp |= 2;
                            break;
                        }
                    }
                }
            }
            ptr += n;
            if (next_state == 1)
                next_state = 0;
            break;
        }
        case 2:
            if (name == "Y")
            {
                std::memcpy(&y, ptr, info[0]);
                y += 4;
            }
            ptr += info[0];
            if (!sinfo_set)
                state = 0;
            break;
        case 3:
        {
            uint32_t n;
            std::memcpy(&n, ptr, 4);
            n = std::byteswap(n);
            ptr += 4;
            if (interesting.contains(name))
            {
                if (name == "WORLD_SURFACE")
                {
                    map_set = true;
                    for (int j = 0; j < n; j++)
                    {
                        std::memcpy(&heightmap[j], ptr, 8);
                        ptr += 8;
                        heightmap[j] = std::byteswap(heightmap[j]);
                    }
                }
                else if (name == "data")
                {
                    if (n)
                        b_temp_set = true;
                    blocks_temp.resize(n);
                    for (int j = 0; j < n; j++)
                    {
                        std::memcpy(&blocks_temp[j], ptr, 8);
                        ptr += 8;
                        blocks_temp[j] = std::byteswap(blocks_temp[j]);
                    }
                }
                else
                {
                    std::array<int, 3> sinfo;
                    std::copy_n(info.begin(), 2, sinfo.begin());
                    sinfo[2] = 0;
                    parse(n, sinfo, name);
                }
            }
            else
                ptr += n * info[1];
            if (!sinfo_set)
                state = 0;
            break;
        }
        case 4:
        {
            std::array<int, 3> sinfo = get_state(*ptr);
            ptr++;
            uint32_t n;
            std::memcpy(&n, ptr, 4);
            n = std::byteswap(n);
            ptr += 4;
            if (interesting.contains(name))
                parse(n, sinfo, name);
            else
                parse_none(n, sinfo);
            if (!sinfo_set)
                state = 0;
            break;
        }
        case 5:
            if (interesting.contains(name))
            {
                if (name == "palette")
                {
                    parse(-1, {0, 0, 0}, name);
                    palette_temp.push_back(process_name(name_temp, prop_temp));
                    prop_temp = 0;
                }
                else if (name == "sections")
                {
                    parse(-1, {0, 0, 0}, name);
                    if (y < 25)
                    {
                        std::swap(palette_temp, palette[y]);
                        palette_temp.clear();
                        std::swap(blocks_temp, blocks[y]);
                        blocks_temp.clear();
                        blocks_set[y] = b_temp_set;
                        b_temp_set = false;
                    }
                }
                else
                    parse(-1, {0, 0, 0}, name);
            }
            else
                parse_none(-1, {0, 0, 0});
            if (!sinfo_set)
                state = 0;
            break;
        }
    }
}

inline void create_colours(std::vector<int> &heightline, const int &skip, const int &offset)
{
    // use the heightmap and parsed data to set the colours for the map
    int i = 1;
    if (!map_set)
        return;
    for (const auto &height : heightmap)
    {
        int m = 0;
        for (int j = 0; j < 7; j++)
        {
            if (i > 256)
                break;
            int h = (height >> m) & 511;
            h--;
            int c = 0;
            int depth = 0;
            while (!c || depth)
            {
                if (h < 0)
                    break;
                int h2 = h >> 4;
                if (!blocks_set[h2])
                {
                    if (palette[h2].size())
                        c = palette[h2][0] & 63;
                    else
                        c = 0;
                    h--;
                    continue;
                }
                int index = ((h & 15) << 8) + i;
                int n = std::max(4, std::bit_width<unsigned>(palette[h2].size() - 1));
                int d = 64 / n;
                int index2 = (index + d - 1) / d - 1;
                int shift = ((index % d) - 1) * n;
                if (shift < 0)
                    shift = n * (d - 1);
                index = (blocks[h2][index2] >> shift) & ((1 << n) - 1);
                c = palette[h2][index];
                int f = c >> 6;
                c = c & 63;
                h--;
                if (f)
                {
                    if (depth)
                        depth++;
                    else if (f & 1)
                        depth = 1;
                }
                else if (depth)
                    break;
            }
            int w = skip + ((i - 1) & 15);
            int z = offset + ((i - 1) >> 4);
            if (depth)
            {
                h += depth - 1;
                depth += (((i - 1 >> 4) ^ i) & 1) << 1;
                if (depth < 5)
                    output[z][w] = (2 << 6) | (COLOURS.at("water") & 255);
                else if (depth > 9)
                    output[z][w] = (COLOURS.at("water") & 255);
                else
                    output[z][w] = (1 << 6) | (COLOURS.at("water") & 255);
            }
            else if (h < heightline[w])
                output[z][w] = c;
            else if (h == heightline[w])
                output[z][w] = (1 << 6) | c;
            else
                output[z][w] = (2 << 6) | c;
            heightline[w] = h;
            i++;
            m += 9;
        }
    }
}

inline void write_file(std::vector<std::vector<uint8_t>> &data)
{
    // compresses and writes the png file
    std::ofstream file;
    file.open("output.png", std::ios::binary);
    file.write(reinterpret_cast<char *>(FORMAT.data()), FORMAT.size());
    file.close();

    file.open("output.png", std::ios::binary | std::ios::app);
    uint32_t space = 1;
    std::vector<uint8_t> data2(space);
    z_stream strm{};
    size_t index = 0;
    size_t block_num = data.size() - 1;
    size_t block_size = data[0].size();
    strm.next_in = &data[0][0];
    strm.avail_in = block_size;
    deflateInit(&strm, 9);
    bool ended = false;
    bool flushing = false;
    while (true)
    {
        uint32_t zindex = 0;
        uint32_t have = space;
        strm.next_out = &data2[0];
        strm.avail_out = space;

        while (true)
        {
            if (flushing)
            {
                if (deflate(&strm, Z_FINISH) == Z_STREAM_END)
                {
                    ended = true;
                    break;
                }
            }
            else
                deflate(&strm, Z_NO_FLUSH);
            if (!strm.avail_out)
            {
                if (space < 4294967295)
                {
                    zindex = space - strm.avail_out;
                    have = space + strm.avail_out;
                    if (space == 2147483648)
                    {
                        space = 4294967295;
                        have--;
                    }
                    else
                        space <<= 1;
                    data2.resize(space);
                    strm.next_out = &data2[zindex];
                    strm.avail_out = have;
                }
                else
                    break;
            }
            else if (!strm.avail_in)
            {
                if (index < block_num)
                {
                    strm.next_in = &data[++index][0];
                    strm.avail_in = block_size;
                }
                else
                    flushing = true;
            }
        }
        uint32_t size = data2.size() - strm.avail_out;
        uint32_t size2 = std::byteswap(size);

        file.write(reinterpret_cast<char *>(&size2), 4);
        uint32_t crc = crc32(0L, Z_NULL, 0);
        file.write("IDAT", 4);
        crc = crc32(crc, reinterpret_cast<const Bytef *>("IDAT"), 4);
        file.write(reinterpret_cast<char *>(data2.data()), size);
        crc = crc32(crc, reinterpret_cast<const Bytef *>(data2.data()), size);
        crc = std::byteswap(crc);
        file.write(reinterpret_cast<char *>(&crc), 4);
        if (ended)
            break;
    }
    inflateEnd(&strm);
    file.write("\0\0\0\0IEND\xae\x42\x60\x82", 12);
    file.close();
}

int main()
{
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    std::cout << "Collecting region files...\n";
    std::vector<std::array<int, 2>> regions;
    int bounds[4];
    bool initial = false;
    int num_regions = 0;
    std::cout << "Collected: " << num_regions;
    for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::current_path()))
    {
        if (entry.is_regular_file())
        {
            std::string file_name = entry.path().filename().string();
            if (!file_name.ends_with(".mca"))
                continue;
            std::istringstream ss(file_name);
            std::array<int, 2> parts;
            std::string token;

            int i = 0;
            while (std::getline(ss, token, '.'))
            {
                if (i && i < 3)
                {
                    int value = std::stoi(token);
                    if (i == 1)
                    {
                        if (initial)
                        {
                            bounds[0] = std::min(bounds[0], value);
                            bounds[1] = std::max(bounds[1], value);
                        }
                        else
                        {
                            bounds[0] = value;
                            bounds[1] = value;
                        }
                    }
                    else
                    {
                        if (initial)
                        {
                            bounds[2] = std::min(bounds[2], value);
                            bounds[3] = std::max(bounds[3], value);
                        }
                        else
                        {
                            bounds[2] = value;
                            bounds[3] = value;
                            initial = true;
                        }
                    }
                    parts[i - 1] = std::stoi(token);
                }
                i++;
            }
            regions.push_back(parts);
            std::cout << "\rCollected: " << ++num_regions << std::flush;
        }
    }

    std::sort(regions.begin(), regions.end(), [](const std::array<int, 2> &a, const std::array<int, 2> &b) {
        if (a[1] == b[1])
            return a[0] < b[0];
        return a[1] < b[1]; });
    int rangex = bounds[1] - bounds[0] + 1;
    int rangez = bounds[3] - bounds[2] + 1;

    // Setting up png file header
    uint32_t width = rangex << 9;
    uint32_t height = rangez << 9;
    uint64_t image_size = static_cast<uint64_t>(width) * height;
    width = std::byteswap(width);
    height = std::byteswap(height);
    std::memcpy(&FORMAT[16], &width, 4);
    std::memcpy(&FORMAT[20], &height, 4);

    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef *>(&FORMAT[12]), 17);
    crc = std::byteswap(crc);
    std::memcpy(&FORMAT[29], &crc, 4);

    std::vector<int> heightline(512 * rangex + 1, -1);
    std::cout << "\nOutput image size will be " << image_size << " pixels.\n";
    output.resize((rangez << 9), std::vector<uint8_t>((rangex << 9) + 1));

    std::cout << "Processing region files...\n";
    std::vector<uint8_t> chunk;
    int count = 0;
    int space = 1;
    std::vector<uint8_t> chunk2(space);
    for (const auto &region : regions)
    {
        std::cout << "\rProcessed: " << ++count << "/" << num_regions << std::flush;
        std::ostringstream oss;
        oss << "r." << region[0] << "." << region[1] << ".mca";
        std::ifstream file(oss.str(), std::ios::binary);
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        if (file_size == 0)
            continue;
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(file_size);
        file.read(reinterpret_cast<char *>(&data[0]), file_size);
        file.close();
        for (int i = 0; i < 1024; i++)
        {
            map_set = false;
            std::memset(blocks_set, 0, 25);
            int i2 = ((i >> 5) << 7) + ((i & 31) << 2);
            uint32_t index;
            std::memcpy(&index, data.data() + i2, 4);
            index = std::byteswap(index);
            if (index)
            {
                index = (index >> 8) << 12;
                uint32_t length;
                std::memcpy(&length, data.data() + index, 4);
                length = std::byteswap(length) - 1;
                index += 5;
                if (length > chunk.size())
                    chunk.resize(1 << std::bit_width<unsigned>(length));
                std::vector<uint8_t> chunk(length);
                std::memcpy(&chunk[0], data.data() + index, length);
                z_stream strm{};
                strm.next_in = chunk.data();
                strm.avail_in = length;
                inflateInit(&strm);
                int zindex = 0;
                int have = space;
                while (true)
                {
                    strm.next_out = &chunk2[zindex];
                    strm.avail_out = have;
                    if (inflate(&strm, Z_NO_FLUSH) == Z_STREAM_END)
                        break;
                    zindex = space - strm.avail_out;
                    have = space + strm.avail_out;
                    space <<= 1;
                    chunk2.resize(space);
                }
                inflateEnd(&strm);
                ptr = &chunk2[3];
                parse(1, {5, 0, 0}, "Y");
                create_colours(heightline, ((region[0] - bounds[0]) << 9) + ((i & 31) << 4) + 1, ((region[1] - bounds[2]) << 9) + ((i >> 5) << 4));
            }
        }
    }
    std::cout << "\nCreating image...\n";
    write_file(output);
    std::cout << "Done.\nExecution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start).count() << " ms" << std::endl;
    return 0;
}
