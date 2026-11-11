-- setup.js of the JavaScript version
return {
    setup = function(engine)
        assert(engine:compile('page', '<article><h1 x-text="$.title"></h1></article>'))
    end,
}
