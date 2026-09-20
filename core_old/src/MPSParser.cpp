#include "MPSParser.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <map>

namespace firefly {

SparseProblem MPSParser::parse_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filepath);
    return parse_stream(file);
}

SparseProblem MPSParser::parse_string(const std::string& mps_content) {
    std::istringstream stream(mps_content);
    return parse_stream(stream);
}

static std::vector<std::string> split_whitespace(const std::string& s) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (tokenStream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

SparseProblem MPSParser::parse_stream(std::istream& stream) {
    SparseProblem prob;
    std::string line;
    enum class Section { NONE, NAME, ROWS, COLUMNS, RHS, RANGES, BOUNDS };
    Section current_section = Section::NONE;

    std::unordered_map<std::string, int> row_name_to_idx;
    std::unordered_map<std::string, int> col_name_to_idx;
    std::map<int, double> ranges;
    std::string obj_name;

    struct CooEntry { int r, c; double v; };
    std::vector<CooEntry> coo_entries;

    int num_constrs = 0;
    int num_vars = 0;

    bool is_integer_section = false;
    std::vector<bool> temp_is_integer;

    auto get_col_idx = [&](const std::string& cname) {
        if (col_name_to_idx.find(cname) == col_name_to_idx.end()) {
            col_name_to_idx[cname] = num_vars++;
            temp_is_integer.push_back(is_integer_section);
        }
        return col_name_to_idx[cname];
    };

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '*') continue;

        if (line[0] != ' ' && line[0] != '\t') {
            auto tokens = split_whitespace(line);
            if (tokens.empty()) continue;
            std::string sec = tokens[0];
            if (sec == "NAME") {
                current_section = Section::NAME;
                if (tokens.size() > 1) prob.name = tokens[1];
            } else if (sec == "ROWS") current_section = Section::ROWS;
            else if (sec == "COLUMNS") current_section = Section::COLUMNS;
            else if (sec == "RHS") current_section = Section::RHS;
            else if (sec == "RANGES") current_section = Section::RANGES;
            else if (sec == "BOUNDS") current_section = Section::BOUNDS;
            else if (sec == "ENDATA") break;
            continue;
        }

        auto tokens = split_whitespace(line);
        if (tokens.empty()) continue;

        if (current_section == Section::ROWS) {
            std::string sense = tokens[0];
            std::string rname = tokens[1];
            if (sense == "N" && obj_name.empty()) {
                obj_name = rname;
            } else {
                row_name_to_idx[rname] = num_constrs++;
                prob.row_senses.push_back(sense[0]);
            }
        } else if (current_section == Section::COLUMNS) {
            if (tokens.size() >= 3 && (tokens[1] == "'MARKER'" || tokens[1] == "MARKER")) {
                if (tokens[2] == "'INTORG'" || tokens[2] == "INTORG") {
                    is_integer_section = true;
                } else if (tokens[2] == "'INTEND'" || tokens[2] == "INTEND") {
                    is_integer_section = false;
                }
                continue;
            }

            std::string cname = tokens[0];
            int cidx = get_col_idx(cname);
            
            for (size_t i = 1; i + 1 < tokens.size(); i += 2) {
                std::string rname = tokens[i];
                double val = std::stod(tokens[i+1]);
                if (rname == obj_name) {
                    if (prob.obj_coeffs.size() <= cidx) prob.obj_coeffs.resize(cidx + 1, 0.0);
                    prob.obj_coeffs[cidx] += val;
                } else if (row_name_to_idx.count(rname)) {
                    coo_entries.push_back({row_name_to_idx[rname], cidx, val});
                }
            }
        } else if (current_section == Section::RHS) {
            prob.rhs.resize(num_constrs, 0.0);
            for (size_t i = 1; i + 1 < tokens.size(); i += 2) {
                std::string rname = tokens[i];
                double val = std::stod(tokens[i+1]);
                if (row_name_to_idx.count(rname)) {
                    prob.rhs[row_name_to_idx[rname]] = val;
                }
            }
        } else if (current_section == Section::RANGES) {
            for (size_t i = 1; i + 1 < tokens.size(); i += 2) {
                std::string rname = tokens[i];
                double val = std::stod(tokens[i+1]);
                if (row_name_to_idx.count(rname)) {
                    ranges[row_name_to_idx[rname]] = val;
                }
            }
        } else if (current_section == Section::BOUNDS) {
            if (prob.var_lower_bounds.empty()) {
                prob.var_lower_bounds.resize(num_vars, 0.0);
                prob.var_upper_bounds.resize(num_vars, std::numeric_limits<double>::infinity());
            }
            std::string type = tokens[0];
            std::string cname = tokens.size() > 2 ? tokens[2] : "";
            double val = tokens.size() > 3 ? std::stod(tokens[3]) : 0.0;
            if (col_name_to_idx.count(cname)) {
                int cidx = col_name_to_idx[cname];
                if (type == "LO") prob.var_lower_bounds[cidx] = val;
                else if (type == "UP") prob.var_upper_bounds[cidx] = val;
                else if (type == "FX") { prob.var_lower_bounds[cidx] = val; prob.var_upper_bounds[cidx] = val; }
                else if (type == "FR") { prob.var_lower_bounds[cidx] = -std::numeric_limits<double>::infinity(); prob.var_upper_bounds[cidx] = std::numeric_limits<double>::infinity(); }
                else if (type == "MI") { prob.var_lower_bounds[cidx] = -std::numeric_limits<double>::infinity(); }
                else if (type == "PL") { prob.var_upper_bounds[cidx] = std::numeric_limits<double>::infinity(); }
            }
        }
    }

    prob.num_vars = num_vars;
    prob.num_constrs = num_constrs;
    prob.obj_coeffs.resize(num_vars, 0.0);
    prob.rhs.resize(num_constrs, 0.0);
    if (prob.var_lower_bounds.empty()) {
        prob.var_lower_bounds.resize(num_vars, 0.0);
        prob.var_upper_bounds.resize(num_vars, std::numeric_limits<double>::infinity());
    }
    prob.is_integer = temp_is_integer;

    // Process RANGES
    for (auto const& [r, rval] : ranges) {
        if (rval == 0.0) continue;

        char sense = prob.row_senses[r];
        double abs_rval = std::abs(rval);
        
        int s_idx = num_vars++;
        prob.is_integer.push_back(false);
        prob.var_lower_bounds.push_back(0.0);
        prob.var_upper_bounds.push_back(abs_rval);
        prob.obj_coeffs.push_back(0.0);
        
        double coeff = 1.0;
        if (sense == 'L') {
            coeff = 1.0;
        } else if (sense == 'G') {
            coeff = -1.0;
        } else if (sense == 'E') {
            if (rval > 0) coeff = -1.0;
            else if (rval < 0) coeff = 1.0;
        }
        
        prob.row_senses[r] = 'E';
        coo_entries.push_back({r, s_idx, coeff});
    }

    prob.num_vars = num_vars;

    // Convert COO to CSR
    // Sort by row, then by column
    std::sort(coo_entries.begin(), coo_entries.end(), [](const CooEntry& a, const CooEntry& b) {
        if (a.r != b.r) return a.r < b.r;
        return a.c < b.c;
    });

    prob.row_ptr.assign(num_constrs + 1, 0);
    prob.col_idx.reserve(coo_entries.size());
    prob.values.reserve(coo_entries.size());

    int current_nnz = 0;
    for (int r = 0; r < num_constrs; ++r) {
        prob.row_ptr[r] = current_nnz;
        while (current_nnz < coo_entries.size() && coo_entries[current_nnz].r == r) {
            prob.col_idx.push_back(coo_entries[current_nnz].c);
            prob.values.push_back(coo_entries[current_nnz].v);
            current_nnz++;
        }
    }
    prob.row_ptr[num_constrs] = current_nnz;

    return prob;
}

} // namespace firefly
