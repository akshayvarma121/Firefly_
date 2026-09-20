#include "firefly/mps_parser.h"
#include <fstream>
#include <sstream>
#include <cmath>

namespace firefly {

enum class Section {
    START,
    NAME,
    OBJSENSE,
    ROWS,
    COLUMNS,
    RHS,
    RANGES,
    BOUNDS,
    ENDATA
};

Problem MPSParser::parse(const std::string& filepath) {
    Problem prob;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }

    std::string line;
    size_t line_num = 0;
    Section current_section = Section::START;
    bool has_endata = false;
    bool in_integer_marker = false;
    bool is_empty = true;

    auto get_row_index = [&](const std::string& name, size_t ln) {
        auto it = prob.row_to_index.find(name);
        if (it == prob.row_to_index.end()) {
            throw MPSParseException(ln, "Reference to undeclared row: " + name);
        }
        return it->second;
    };

    auto add_col = [&](const std::string& name) {
        auto it = prob.col_to_index.find(name);
        if (it == prob.col_to_index.end()) {
            size_t idx = prob.col_names.size();
            prob.col_names.push_back(name);
            prob.col_to_index[name] = idx;
            prob.col_lower_bounds.push_back(0.0); // default lower bound is 0
            prob.col_upper_bounds.push_back(INF); // default upper bound is infinity
            prob.is_integer.push_back(in_integer_marker);
            prob.objective.push_back(0.0);
            return idx;
        }
        return it->second;
    };

    auto parse_double = [](const std::string& s, size_t ln) {
        try {
            return std::stod(s);
        } catch (...) {
            throw MPSParseException(ln, "Failed to parse numeric value: " + s);
        }
    };

    while (std::getline(file, line)) {
        line_num++;

        if (line.empty() || line[0] == '*') continue;

        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        if (tokens.empty()) continue;
        
        is_empty = false;

        // Section header must start at column 0 (no leading whitespace)
        if (line[0] != ' ' && line[0] != '\t') {
            std::string section_name = tokens[0];
            if (section_name == "NAME") {
                current_section = Section::NAME;
                if (tokens.size() > 1) prob.name = tokens[1];
                continue;
            } else if (section_name == "OBJSENSE") {
                current_section = Section::OBJSENSE;
                continue;
            } else if (section_name == "ROWS") {
                current_section = Section::ROWS;
                continue;
            } else if (section_name == "COLUMNS") {
                current_section = Section::COLUMNS;
                continue;
            } else if (section_name == "RHS") {
                current_section = Section::RHS;
                continue;
            } else if (section_name == "RANGES") {
                current_section = Section::RANGES;
                continue;
            } else if (section_name == "BOUNDS") {
                current_section = Section::BOUNDS;
                continue;
            } else if (section_name == "ENDATA") {
                has_endata = true;
                break;
            } else {
                throw MPSParseException(line_num, "Unknown section header: " + section_name);
            }
        }

        switch (current_section) {
            case Section::START:
                throw MPSParseException(line_num, "Content before NAME section");
            case Section::NAME:
                throw MPSParseException(line_num, "Unexpected content in NAME section");
            case Section::OBJSENSE:
                if (tokens[0] == "MAX" || tokens[0] == "MAXIMIZE") {
                    prob.minimize = false;
                } else if (tokens[0] == "MIN" || tokens[0] == "MINIMIZE") {
                    prob.minimize = true;
                } else {
                    throw MPSParseException(line_num, "Unknown OBJSENSE: " + tokens[0]);
                }
                break;
            case Section::ROWS: {
                if (tokens.size() < 2) throw MPSParseException(line_num, "Malformed ROWS line");
                char sense = tokens[0][0];
                std::string name = tokens[1];
                if (sense != 'N' && sense != 'L' && sense != 'G' && sense != 'E') {
                    throw MPSParseException(line_num, "Unknown row sense: " + tokens[0]);
                }
                
                if (sense == 'N' && prob.objective_name.empty()) {
                    prob.objective_name = name;
                }

                size_t idx = prob.row_names.size();
                prob.row_names.push_back(name);
                prob.row_to_index[name] = idx;
                prob.row_senses.push_back(sense);
                
                if (sense == 'N') {
                    prob.row_lower_bounds.push_back(-INF);
                    prob.row_upper_bounds.push_back(INF);
                } else if (sense == 'L') {
                    prob.row_lower_bounds.push_back(-INF);
                    prob.row_upper_bounds.push_back(0.0);
                } else if (sense == 'G') {
                    prob.row_lower_bounds.push_back(0.0);
                    prob.row_upper_bounds.push_back(INF);
                } else { // E
                    prob.row_lower_bounds.push_back(0.0);
                    prob.row_upper_bounds.push_back(0.0);
                }
                break;
            }
            case Section::COLUMNS: {
                if (tokens.size() < 3) throw MPSParseException(line_num, "Malformed COLUMNS line");
                std::string col_name = tokens[0];

                if (tokens.size() >= 3 && tokens[1] == "'MARKER'") {
                    if (tokens[2] == "'INTORG'") {
                        in_integer_marker = true;
                    } else if (tokens[2] == "'INTEND'") {
                        in_integer_marker = false;
                    } else {
                        throw MPSParseException(line_num, "Unknown marker type: " + tokens[2]);
                    }
                    continue;
                }

                size_t col_idx = add_col(col_name);
                
                std::string row_name1 = tokens[1];
                double val1 = parse_double(tokens[2], line_num);
                if (row_name1 == prob.objective_name) {
                    prob.objective[col_idx] += val1;
                } else {
                    size_t row_idx1 = get_row_index(row_name1, line_num);
                    prob.matrix.push_back({row_idx1, col_idx, val1});
                }

                if (tokens.size() >= 5) {
                    std::string row_name2 = tokens[3];
                    double val2 = parse_double(tokens[4], line_num);
                    if (row_name2 == prob.objective_name) {
                        prob.objective[col_idx] += val2;
                    } else {
                        size_t row_idx2 = get_row_index(row_name2, line_num);
                        prob.matrix.push_back({row_idx2, col_idx, val2});
                    }
                }
                break;
            }
            case Section::RHS: {
                if (tokens.size() < 3) throw MPSParseException(line_num, "Malformed RHS line");
                std::string row_name1 = tokens[1];
                double val1 = parse_double(tokens[2], line_num);
                if (row_name1 != prob.objective_name) {
                    size_t row_idx1 = get_row_index(row_name1, line_num);
                    char sense = prob.row_senses[row_idx1];
                    if (sense == 'L') prob.row_upper_bounds[row_idx1] = val1;
                    else if (sense == 'G') prob.row_lower_bounds[row_idx1] = val1;
                    else if (sense == 'E') {
                        prob.row_lower_bounds[row_idx1] = val1;
                        prob.row_upper_bounds[row_idx1] = val1;
                    }
                }
                if (tokens.size() >= 5) {
                    std::string row_name2 = tokens[3];
                    double val2 = parse_double(tokens[4], line_num);
                    if (row_name2 != prob.objective_name) {
                        size_t row_idx2 = get_row_index(row_name2, line_num);
                        char sense = prob.row_senses[row_idx2];
                        if (sense == 'L') prob.row_upper_bounds[row_idx2] = val2;
                        else if (sense == 'G') prob.row_lower_bounds[row_idx2] = val2;
                        else if (sense == 'E') {
                            prob.row_lower_bounds[row_idx2] = val2;
                            prob.row_upper_bounds[row_idx2] = val2;
                        }
                    }
                }
                break;
            }
            case Section::RANGES: {
                if (tokens.size() < 3) throw MPSParseException(line_num, "Malformed RANGES line");
                
                auto apply_range = [&](const std::string& rname, double rval) {
                    if (rname == prob.objective_name) return;
                    size_t idx = get_row_index(rname, line_num);
                    char sense = prob.row_senses[idx];
                    double b = 0.0;
                    if (sense == 'L') b = prob.row_upper_bounds[idx];
                    else if (sense == 'G') b = prob.row_lower_bounds[idx];
                    else if (sense == 'E') b = prob.row_lower_bounds[idx];

                    if (sense == 'L') {
                        prob.row_lower_bounds[idx] = b - std::abs(rval);
                    } else if (sense == 'G') {
                        prob.row_upper_bounds[idx] = b + std::abs(rval);
                    } else if (sense == 'E') {
                        if (rval > 0) {
                            prob.row_upper_bounds[idx] = b + rval;
                        } else {
                            prob.row_lower_bounds[idx] = b - std::abs(rval);
                        }
                    }
                };

                std::string row_name1 = tokens[1];
                double val1 = parse_double(tokens[2], line_num);
                apply_range(row_name1, val1);
                
                if (tokens.size() >= 5) {
                    std::string row_name2 = tokens[3];
                    double val2 = parse_double(tokens[4], line_num);
                    apply_range(row_name2, val2);
                }
                break;
            }
            case Section::BOUNDS: {
                if (tokens.size() < 3) throw MPSParseException(line_num, "Malformed BOUNDS line");
                std::string type = tokens[0];
                std::string col_name = tokens[2];
                size_t col_idx = add_col(col_name);
                
                if (type == "UP") {
                    if (tokens.size() < 4) throw MPSParseException(line_num, "Missing value for UP bound");
                    prob.col_upper_bounds[col_idx] = parse_double(tokens[3], line_num);
                } else if (type == "LO") {
                    if (tokens.size() < 4) throw MPSParseException(line_num, "Missing value for LO bound");
                    prob.col_lower_bounds[col_idx] = parse_double(tokens[3], line_num);
                } else if (type == "FX") {
                    if (tokens.size() < 4) throw MPSParseException(line_num, "Missing value for FX bound");
                    double val = parse_double(tokens[3], line_num);
                    prob.col_lower_bounds[col_idx] = val;
                    prob.col_upper_bounds[col_idx] = val;
                } else if (type == "FR") {
                    prob.col_lower_bounds[col_idx] = -INF;
                    prob.col_upper_bounds[col_idx] = INF;
                } else if (type == "MI") {
                    prob.col_lower_bounds[col_idx] = -INF;
                } else if (type == "PL") {
                    prob.col_upper_bounds[col_idx] = INF;
                } else if (type == "BV") {
                    prob.col_lower_bounds[col_idx] = 0.0;
                    prob.col_upper_bounds[col_idx] = 1.0;
                    prob.is_integer[col_idx] = true;
                } else {
                    throw MPSParseException(line_num, "Unknown bound type: " + type);
                }
                break;
            }
            default:
                break;
        }
    }

    if (is_empty) {
        throw MPSParseException(0, "Empty file");
    }

    if (!has_endata) {
        throw MPSParseException(line_num, "Missing ENDATA");
    }

    return prob;
}

} // namespace firefly
