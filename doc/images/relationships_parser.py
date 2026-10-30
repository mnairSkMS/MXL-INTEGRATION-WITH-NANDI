#!/bin/python
import sys

#Read Class Names from class_names.txt
class_names_file = "class_names.txt"
existing_class_names = []
with open(class_names_file, 'r') as file:
    for line in file:
        stripped_line = line.strip()
        #print(stripped_line)
        existing_class_names.append(stripped_line)
    
        
#if "S3OutputTransportParameters" in existing_class_names:
    #print("S3OutputTransportParameters found")
    #exit(1)

#exit(1)
#print(f"Existing Classes: {len(existing_class_names)}")
#print(existing_class_names)

#Read input file line by line and analyze
file_path = sys.argv[1]
current_class_name = ""
with open(file_path, 'r') as file:
    for line in file:
        
        stripped_line = line.strip()
        word_list = stripped_line.split()
        
        if len(word_list) == 0:
            continue

        if word_list[0] == "class":
            current_class_name = word_list[1]
            #print(f"Current Class: {current_class_name}")
        
        #print(f"Checking if {word_list[0]} is in existing_class_names")

        if word_list[0] in existing_class_names:
            print(f"{word_list[0]} -- {current_class_name}")
