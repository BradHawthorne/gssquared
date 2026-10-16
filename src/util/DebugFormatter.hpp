/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include "../a2gspu_ctrl_json.hpp"

class DebugFormatter {
private:
    std::vector<std::string> lines;
    
public:
    struct Field { std::string name,type,value_json,unit; };
    std::vector<Field> fields;
    void field(const std::string &name,const std::string &type,const std::string &value,const std::string &unit="raw") {
        for(const auto &f:fields)if(f.name==name)throw std::runtime_error("duplicate device field: "+name);
        fields.push_back({name,type,value,unit});
    }
    void unsignedField(const std::string &name,uint64_t value,unsigned bits=32,const std::string &unit="raw") {
        field(name,"u"+std::to_string(bits),bits>32?a2ctrljson::quote(std::to_string(value)):std::to_string(value),unit);
    }
    void boolField(const std::string &name,bool value){field(name,"bool",value?"true":"false");}
    void signedField(const std::string &name,int64_t value,unsigned bits=32,const std::string &unit="raw") {
        field(name,"i"+std::to_string(bits),bits>32?a2ctrljson::quote(std::to_string(value)):std::to_string(value),unit);
    }
    void stringField(const std::string &name,const std::string &value){field(name,"string",a2ctrljson::quote(value));}
    void bytesField(const std::string &name,const uint8_t *value,size_t count) {
        std::string json="[";for(size_t i=0;i<count;i++){if(i)json+=',';json+=std::to_string(value[i]);}
        field(name,"bytes",json+"]","bytes");
    }
    // Printf-style formatting for adding a line
    template<typename... Args>
    void addLine(const char* format, Args... args) {
        constexpr size_t buffer_size = 1024;
        char buffer[buffer_size];
        snprintf(buffer, buffer_size, format, args...);
        lines.push_back(std::string(buffer));
    }

    void print() {
        for (const auto& line : lines) {
            printf("%s\n", line.c_str());
        }
    }
    
    // Add a plain string line
    void addLine(const std::string& line) {
        lines.push_back(line);
    }
    
    // Add a plain C-string line
    void addLine(const char* line) {
        lines.push_back(std::string(line));
    }
    
    // Get the vector of formatted lines
    const std::vector<std::string>& getLines() const {
        return lines;
    }
    
    // Clear all lines
    void clear() {
        lines.clear();
        fields.clear();
    }
    
    // Get number of lines
    size_t size() const {
        return lines.size();
    }
    
    // Check if empty
    bool empty() const {
        return lines.empty();
    }
};
