all:
	lualatex -shell-escape rapport.tex
	biber rapport
	lualatex -shell-escape rapport.tex
	lualatex -shell-escape rapport.tex
	evince rapport.pdf &

main:
	pdflatex main.tex && evince main.pdf &

clean:
	rm *.aux *.log *.pdf