-- Mirrors js-reflow/test/fixtures/valid/x-include-basic/setup.js.
-- The fixture runner passes the Reflow instance so setup can register
-- additional templates before compile+render runs against `main`.
return function(reflow)
    reflow:compile('page',
        '<article><h1 x-text="$.title"></h1></article>')
end
