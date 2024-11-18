#!/usr/bin/env python3

# Python script for generating new Jaffx projects

import os
import sys
import shutil

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
    
    # Copy template files
    for filename in os.listdir(template_dir):
        src_path = os.path.join(template_dir, filename)
        dst_path = os.path.join(new_project_dir, filename)
        
        # Copy file
        shutil.copy2(src_path, dst_path)
        
        # Read and replace contents
        with open(dst_path, 'r') as f:
            content = f.read()
        
        # Replace template with project name (both lowercase and capitalized)
        content = content.replace('template', project_name)
        content = content.replace('Template', project_name.capitalize())
        
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
