import os
import re
import glob

def refactor_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # 1. Replace assert( with FIREFLY_TEST_ASSERT(
    # Avoid replacing if it's already replaced
    if 'FIREFLY_TEST_ASSERT' not in content:
        content = re.sub(r'\bassert\s*\(', 'FIREFLY_TEST_ASSERT(', content)
    
    # 2. Add #include "test_utils.h" and remove <cassert>
    if 'FIREFLY_TEST_ASSERT' in content and '"test_utils.h"' not in content:
        content = re.sub(r'#include\s*<cassert>', '#include "test_utils.h"', content)
        if '#include "test_utils.h"' not in content:
            # If no <cassert> was included, add it after the first include
            content = re.sub(r'(#include\s*<[^>]+>)', r'\1\n#include "test_utils.h"', content, count=1)
            
    # 3. Insert MSVC boilerplate into main()
    msvc_include = "#ifdef _MSC_VER\n#include <crtdbg.h>\n#endif\n"
    msvc_setup = """
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
"""
    if '<crtdbg.h>' not in content:
        # Add after test_utils.h or first include
        if '#include "test_utils.h"' in content:
            content = content.replace('#include "test_utils.h"', '#include "test_utils.h"\n' + msvc_include)
        else:
            content = re.sub(r'(#include\s*<[^>]+>)', r'\1\n' + msvc_include, content, count=1)
            
    # Add setup at the start of main
    if '_set_abort_behavior' not in content:
        # Find main function
        main_pattern = r'(int\s+main\s*\([^)]*\)\s*\{)'
        content = re.sub(main_pattern, r'\1' + msvc_setup, content)
        
    with open(filepath, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    files = glob.glob("*.cpp")
    for f in files:
        if f != "test_utils.h" and f != "refactor_asserts.py":
            print(f"Processing {f}")
            refactor_file(f)
