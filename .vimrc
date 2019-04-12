" Indent using two spaces per level
set expandtab
set shiftwidth=2
set softtabstop=2

" Delete trailing whitespace when saving files
autocmd BufWritePre * :%s/\s\+$//e

" Set correct syntax highlighting for C
augroup project
    autocmd!
    autocmd BufRead,BufNewFile *.h,*.c set filetype=c
augroup END

