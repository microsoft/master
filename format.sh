#!/bin/bash

for f in $(find . -path ./build -prune -o -path ./third_party -prune -o -name '*.h' -or -name '*.c' -or -name '*.cpp' -or -name '*.cc'); do 
    if [ ${f} != './build' -a ${f} != './third_party' ]; then
        echo "Formatting ${f}"
        clang-format -i --style=file ${f}
    fi
done

echo "Done!"
