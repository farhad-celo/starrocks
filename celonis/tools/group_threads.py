"""Groups threads with similar stack traces from the output of `thread apply all bt` in GDB.

Starrocks BE has over a thousand threads. So it is very hard to debug escpeically when we don't know which thread we
need to investigate. This groups threads with similar stack traces so that it helps to identify which groups to look
into. In a debugging, it grouped 1752 threads into 64 groups.

Usage: python group_threads.py <input_file> <output_file>

Example usage:
gdb --batch -s starrocks_be.debuginfo.image_tag -e starrocks_be.image_tag -c coredump -ex "thread apply all bt" > threads.txt
python group_threads.py threads.txt grouped_threads.txt

Example result: https://gist.github.com/jkim650/f3b2deab979fed193e1750a88a5649d8
"""

import re
import sys
from collections import defaultdict

def group_threads_by_stack(input_file, output_file):
    thread_stacks = defaultdict(list)

    with open(input_file, 'r') as f:
        current_thread = None
        current_stack = []

        for line in f:
            if line.startswith('Thread'):
                if current_thread:
                    stack_hash = tuple(current_stack)
                    thread_stacks[stack_hash].append(current_thread)
                current_thread = int(re.findall(r'\d+', line)[0])
                current_stack = []
            elif line.startswith('#'):
                words = line.strip().split()
                if words[2] == "in":
                    if words[3] == "??" and words[1] != "0x0000000000000000":
                        words[1] = "0xXXXXXXXXXXXXXXXX"
                    stack_line = ' '.join(words[:4])
                else:
                    stack_line = ' '.join(words[:2])
                current_stack.append(stack_line)

        if current_thread:
            stack_hash = tuple(current_stack)
            thread_stacks[stack_hash].append(current_thread)

    # Sort the groups by the number of threads in descending order, then by stack depth in descending order
    sorted_groups = sorted(thread_stacks.items(), key=lambda x: (-len(x[1]), -len(x[0])))

    with open(output_file, 'w') as f:
        for stack, threads in sorted_groups:
            f.write(f"Threads with the similar stack trace({len(threads)}): {', '.join(map(str, threads))}\n")
            f.write('\n'.join(stack) + '\n\n')

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python group_threads.py <input_file> <output_file>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    group_threads_by_stack(input_file, output_file)
