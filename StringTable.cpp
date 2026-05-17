#include "StringTable.h"
#include <fstream>
#include <sstream>
#include <algorithm>

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------
StringTable::StringTable() {
    m_categories.push_back("default");
}

// -----------------------------------------------------------------------
// Categories
// -----------------------------------------------------------------------
void StringTable::addCategory(const std::string& name) {
    m_categories.push_back(name);
}

bool StringTable::removeCategory(int index) {
    if (index <= 0 || index >= (int)m_categories.size())
        return false;
    int oldIdx = index;
    m_categories.erase(m_categories.begin() + index);
    for (auto& kc : m_keyCategories) {
        if (kc == oldIdx)
            kc = 0;
        else if (kc > oldIdx)
            --kc;
    }
    return true;
}

int StringTable::getCategoryCount() const { return (int)m_categories.size(); }
const std::string& StringTable::getCategory(int index) const { return m_categories[index]; }

// -----------------------------------------------------------------------
// Regions
// -----------------------------------------------------------------------
void StringTable::addRegion(const std::string& name) {
    m_regions.push_back(name);
    m_data.emplace_back(m_keys.size());
}

bool StringTable::removeRegion(int index) {
    if (index < 0 || index >= (int)m_regions.size())
        return false;
    m_regions.erase(m_regions.begin() + index);
    m_data.erase(m_data.begin() + index);
    return true;
}

bool StringTable::renameRegion(int index, const std::string& newName) {
    if (index < 0 || index >= (int)m_regions.size())
        return false;
    m_regions[index] = newName;
    return true;
}

// -----------------------------------------------------------------------
// Keys
// -----------------------------------------------------------------------
void StringTable::addKey(const std::string& name, int categoryIndex) {
    m_keys.push_back(name);
    m_keyCategories.push_back(categoryIndex);
    for (auto& row : m_data)
        row.resize(m_keys.size());
}

bool StringTable::removeKey(int index) {
    if (index < 0 || index >= (int)m_keys.size())
        return false;
    m_keys.erase(m_keys.begin() + index);
    m_keyCategories.erase(m_keyCategories.begin() + index);
    for (auto& row : m_data)
        row.erase(row.begin() + index);
    return true;
}

bool StringTable::renameKey(int index, const std::string& newName) {
    if (index < 0 || index >= (int)m_keys.size())
        return false;
    m_keys[index] = newName;
    return true;
}

int StringTable::getKeyCategory(int keyIndex) const {
    return m_keyCategories[keyIndex];
}

void StringTable::setKeyCategory(int keyIndex, int catIndex) {
    m_keyCategories[keyIndex] = catIndex;
}

int StringTable::getRegionCount() const { return (int)m_regions.size(); }
int StringTable::getKeyCount() const { return (int)m_keys.size(); }
const std::string& StringTable::getRegion(int index) const { return m_regions[index]; }
const std::string& StringTable::getKey(int index) const { return m_keys[index]; }

std::string StringTable::getText(int regionIndex, int keyIndex) const {
    if (regionIndex < 0 || regionIndex >= (int)m_data.size())
        return {};
    if (keyIndex < 0 || keyIndex >= (int)m_data[regionIndex].size())
        return {};
    return m_data[regionIndex][keyIndex];
}

void StringTable::setText(int regionIndex, int keyIndex, const std::string& text) {
    if (regionIndex < 0 || regionIndex >= (int)m_data.size())
        return;
    if (keyIndex < 0 || keyIndex >= (int)m_data[regionIndex].size())
        return;
    m_data[regionIndex][keyIndex] = text;
}

// -----------------------------------------------------------------------
// Export / Import
// -----------------------------------------------------------------------
bool StringTable::exportToFile(const std::string& filePath) const {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return false;

    // Categories header
    file << "@Categories\n";
    for (int c = 0; c < (int)m_categories.size(); ++c) {
        if (c > 0) file << ',';
        file << m_categories[c];
    }
    file << "\n\n";

    // Keys header
    file << "@Keys\n";
    for (int k = 0; k < (int)m_keys.size(); ++k) {
        file << m_keys[k] << ':' << m_categories[m_keyCategories[k]] << '\n';
    }
    file << '\n';

    // Data: each key appears once with its region values
    for (int k = 0; k < (int)m_keys.size(); ++k) {
        file << '[' << m_keys[k] << "]\n";
        for (int r = 0; r < (int)m_regions.size(); ++r) {
            file << m_regions[r] << " = " << m_data[r][k] << '\n';
        }
        file << '\n';
    }
    return true;
}

static int findOrAdd(std::vector<std::string>& vec, const std::string& name) {
    auto it = std::find(vec.begin(), vec.end(), name);
    if (it != vec.end())
        return (int)(it - vec.begin());
    vec.push_back(name);
    return (int)vec.size() - 1;
}

bool StringTable::importFromFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    clear();

    // Detect and skip UTF-8 BOM
    char bom[3];
    if (file.read(bom, 3) && !(bom[0] == '\xEF' && bom[1] == '\xBB' && bom[2] == '\xBF'))
        file.seekg(0);

    std::string line;
    bool inCategories = false;
    bool inKeys = false;
    int currentKey = -1;
    int currentRegion = -1;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) {
            inCategories = false;
            inKeys = false;
            continue;
        }

        // Section headers
        if (line == "@Categories") { inCategories = true; inKeys = false; continue; }
        if (line == "@Keys")        { inKeys = true; inCategories = false; continue; }

        if (inCategories) {
            std::istringstream ss(line);
            std::string cat;
            bool first = true;
            while (std::getline(ss, cat, ',')) {
                cat = trim(cat);
                if (!cat.empty()) {
                    if (first && m_categories.size() == 1 &&
                        m_categories[0] == "default" && m_keys.empty()) {
                        // first import — replace default name
                        m_categories[0] = cat;
                    } else {
                        m_categories.push_back(cat);
                    }
                    first = false;
                }
            }
            continue;
        }

        if (inKeys) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string cat = trim(line.substr(colon + 1));
                int ci = 0;
                auto it = std::find(m_categories.begin(), m_categories.end(), cat);
                if (it != m_categories.end())
                    ci = (int)(it - m_categories.begin());
                addKey(key, ci);
            }
            continue;
        }

        // Data sections:
        //   [known_key]  → key-centric (region = value)
        //   [other]      → region-centric (key = value)
        if (line.front() == '[' && line.back() == ']') {
            std::string name = trim(line.substr(1, line.size() - 2));
            if (name.empty()) continue;

            auto kit = std::find(m_keys.begin(), m_keys.end(), name);
            if (kit != m_keys.end()) {
                currentKey = (int)(kit - m_keys.begin());
                currentRegion = -1;
            } else {
                currentRegion = findOrAdd(m_regions, name);
                // Ensure data matrix has the right size
                while ((int)m_data.size() < (int)m_regions.size())
                    m_data.emplace_back(m_keys.size());
                currentKey = -1;
            }
            continue;
        }

        // name = value
        size_t eq = line.find(" = ");
        if (eq != std::string::npos) {
            std::string name = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 3));
            if (name.empty()) continue;

            if (currentKey >= 0) {
                // key-centric: name is a region
                int ri = findOrAdd(m_regions, name);
                while ((int)m_data.size() <= ri)
                    m_data.emplace_back(m_keys.size());
                setText(ri, currentKey, val);
            } else if (currentRegion >= 0) {
                // region-centric: name is a key
                auto ki = std::find(m_keys.begin(), m_keys.end(), name);
                int kidx;
                if (ki != m_keys.end()) {
                    kidx = (int)(ki - m_keys.begin());
                } else {
                    addKey(name);
                    kidx = (int)m_keys.size() - 1;
                }
                setText(currentRegion, kidx, val);
            }
        }
    }
    return true;
}

void StringTable::clear() {
    m_categories.assign(1, "default");
    m_keyCategories.clear();
    m_regions.clear();
    m_keys.clear();
    m_data.clear();
}
