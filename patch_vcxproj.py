import os
import re

build_dir = "build"

def patch_file(filepath):
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    # Find <CudaCompile> blocks in MSBuild project file
    cuda_compile_pattern = re.compile(r'(<CudaCompile>[\s\S]*?</CudaCompile>)')
    
    modified = False
    def replace_ehsc(match):
        nonlocal modified
        block = match.group(1)
        opt_pattern = re.compile(r'(<AdditionalOptions>)(.*?)(</AdditionalOptions>)')
        
        def replace_opts(opt_match):
            nonlocal modified
            start, opts, end = opt_match.groups()
            new_opts = opts
            
            # Remove standalone /EHsc and /MP which NVCC fails to parse
            for bad_flag in [' /EHsc', ' /MP', '/EHsc ', '/MP ']:
                if bad_flag in new_opts:
                    new_opts = new_opts.replace(bad_flag, ' ')
                    modified = True
                    
            # Cleanup multiple spaces
            new_opts = ' '.join(new_opts.split())
            
            return start + new_opts + end

        new_block = opt_pattern.sub(replace_opts, block)
        return new_block

    new_content = cuda_compile_pattern.sub(replace_ehsc, content)
    
    if modified:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"Patched {filepath} successfully!")

if __name__ == '__main__':
    if os.path.exists(build_dir):
        for root, dirs, files in os.walk(build_dir):
            for file in files:
                if file.endswith('.vcxproj'):
                    patch_file(os.path.join(root, file))
    else:
        print(f"Build directory '{build_dir}' does not exist.")
