-- Mirrors js-reflow/test/fixtures/valid/x-with-include/setup.js.
return function(reflow)
    reflow:compile('panel',
        '<section><h2 x-text="@title"></h2>' ..
        '<p x-text="@body"></p></section>')
end
