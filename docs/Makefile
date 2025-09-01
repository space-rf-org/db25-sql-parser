PDFLATEX = pdflatex
XELATEX = xelatex
LUALATEX = lualatex
BIBTEX = bibtex
PAPERS = arena_allocator_paper AST_Design_Column_References

.PHONY: all clean view help ast

all: $(addsuffix .pdf, $(PAPERS))

ast: AST_Design_Column_References.pdf

# Use LuaLaTeX for the AST document (Full Unicode and emoji support)
AST_Design_Column_References.pdf: AST_Design_Column_References.tex
	@echo "Building AST Design document with LuaLaTeX (Full Unicode + Emoji support)..."
	@$(LUALATEX) -interaction=nonstopmode $< > /dev/null 2>&1
	@$(LUALATEX) -interaction=nonstopmode $< > /dev/null 2>&1
	@echo "PDF generated: $@"

# Use pdfLaTeX for the arena allocator paper
arena_allocator_paper.pdf: arena_allocator_paper.tex
	$(PDFLATEX) arena_allocator_paper
	$(PDFLATEX) arena_allocator_paper
	$(PDFLATEX) arena_allocator_paper

clean:
	rm -f *.pdf *.aux *.log *.nav *.out *.snm *.toc *.bbl *.blg *.dvi *.ps *.brf *.lof *.lot *.fls *.fdb_latexmk *.synctex.gz

view: AST_Design_Column_References.pdf
	open AST_Design_Column_References.pdf

view-arena: arena_allocator_paper.pdf
	open arena_allocator_paper.pdf

help:
	@echo "Usage:"
	@echo "  make         - Compile all papers to PDF"
	@echo "  make ast     - Compile AST design document to PDF"
	@echo "  make clean   - Remove all generated files"
	@echo "  make view    - Compile and open the AST design PDF"
	@echo "  make help    - Show this help message"