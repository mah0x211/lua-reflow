-- setup.js of the JavaScript version
return {
    setup = function(engine)
        assert(engine:compile('panel',
                              '<section><h2 x-text="@title"></h2><p x-text="@body"></p></section>'))
    end,
}
