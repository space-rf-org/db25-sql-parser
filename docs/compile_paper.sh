#!/bin/bash

# DB25 Arena Allocator Paper Compilation Script
# Handles Unicode and ensures proper compilation

echo "Compiling DB25 Arena Allocator Paper..."

# Method 1: Using pdflatex with proper encoding
echo "Method 1: Attempting pdflatex compilation..."
pdflatex -interaction=nonstopmode -halt-on-error arena_allocator_paper.tex

if [ $? -eq 0 ]; then
    # Run twice more for references
    pdflatex -interaction=nonstopmode arena_allocator_paper.tex
    pdflatex -interaction=nonstopmode arena_allocator_paper.tex
    echo "✓ PDF generated successfully with pdflatex"
    echo "Output: arena_allocator_paper.pdf"
else
    echo "pdflatex failed, trying alternative methods..."
    
    # Method 2: Try XeLaTeX (better Unicode support)
    if command -v xelatex &> /dev/null; then
        echo "Method 2: Attempting XeLaTeX compilation..."
        xelatex -interaction=nonstopmode arena_allocator_paper.tex
        if [ $? -eq 0 ]; then
            xelatex -interaction=nonstopmode arena_allocator_paper.tex
            echo "✓ PDF generated successfully with XeLaTeX"
        fi
    fi
    
    # Method 3: Try LuaLaTeX
    if command -v lualatex &> /dev/null; then
        echo "Method 3: Attempting LuaLaTeX compilation..."
        lualatex -interaction=nonstopmode arena_allocator_paper.tex
        if [ $? -eq 0 ]; then
            lualatex -interaction=nonstopmode arena_allocator_paper.tex
            echo "✓ PDF generated successfully with LuaLaTeX"
        fi
    fi
fi

# Clean up auxiliary files
echo "Cleaning up auxiliary files..."
rm -f *.aux *.log *.nav *.out *.snm *.toc *.bbl *.blg *.dvi *.ps *.brf *.lof *.lot *.fls *.fdb_latexmk *.synctex.gz

echo "Done!"

# Check if PDF was created
if [ -f arena_allocator_paper.pdf ]; then
    echo "Success! PDF size: $(ls -lh arena_allocator_paper.pdf | awk '{print $5}')"
else
    echo "Error: PDF was not generated. Check the log file for details."
    exit 1
fi