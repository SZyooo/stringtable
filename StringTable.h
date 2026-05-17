#pragma once
#include <string>
#include <vector>

class StringTable {
public:
    StringTable();

    void addCategory(const std::string& name);
    bool removeCategory(int index);
    int getCategoryCount() const;
    const std::string& getCategory(int index) const;

    void addRegion(const std::string& name);
    bool removeRegion(int index);
    bool renameRegion(int index, const std::string& newName);

    void addKey(const std::string& name, int categoryIndex = 0);
    bool removeKey(int index);
    bool renameKey(int index, const std::string& newName);
    int getKeyCategory(int keyIndex) const;
    void setKeyCategory(int keyIndex, int catIndex);

    int getRegionCount() const;
    int getKeyCount() const;
    const std::string& getRegion(int index) const;
    const std::string& getKey(int index) const;

    std::string getText(int regionIndex, int keyIndex) const;
    void setText(int regionIndex, int keyIndex, const std::string& text);

    bool exportToFile(const std::string& filePath) const;
    bool importFromFile(const std::string& filePath);
    void clear();

private:
    std::vector<std::string> m_categories;
    std::vector<int> m_keyCategories;
    std::vector<std::string> m_regions;
    std::vector<std::string> m_keys;
    std::vector<std::vector<std::string>> m_data;
};
