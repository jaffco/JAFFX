#!/usr/bin/env python3

# Python script for generating new Jaffx projects

import os
import sys
import shutil
import re

def generate_project(project_name):
    # Define paths
    template_dir = 'src/template'
    new_project_dir = f'src/{project_name}'
    
    # Validate input
    if not project_name:
        print("Usage: python projectGen.py <project_name>")
        sys.exit(1)
    
    # Check template directory exists
    if not os.path.isdir(template_dir):
        print(f"Error: Template directory '{template_dir}' does not exist.")
        sys.exit(1)
    
    # Check if project directory already exists
    if os.path.exists(new_project_dir):
        print(f"Error: Project directory '{new_project_dir}' already exists.")
        sys.exit(1)
    
    # Create new project directory
    os.makedirs(new_project_dir)
    
    # Rename files
    for filename in os.listdir(template_dir):
        # Replace 'template' in filename
        new_filename = filename.replace('template', project_name)
        
        # Copy file with new name
        src_path = os.path.join(template_dir, filename)
        dst_path = os.path.join(new_project_dir, new_filename)
        
        shutil.copy2(src_path, dst_path)
        
        # Read and replace contents
        with open(dst_path, 'r') as f:
            content = f.read()
        
        # Replace class name
        # First letter capitalized version of project name
        class_name = project_name[0].upper() + project_name[1:]
        
        # Replace variations
        content = re.sub(r'\bTemplate\b', class_name, content)
        content = re.sub(r'\btemplate\b', project_name, content)
        
        # Replace instance variable if needed
        content = re.sub(r'm(T|t)emplate', f'm{class_name}', content)
        
        # Write modified content back
        with open(dst_path, 'w') as f:
            f.write(content)
    
    print(f"Project '{project_name}' has been created in '{new_project_dir}'.")

def main():
    if len(sys.argv) < 2:
        print("Usage: python projectGen.py <project_name>")
        sys.exit(1)
    
    generate_project(sys.argv[1])

if __name__ == '__main__':
    main()
