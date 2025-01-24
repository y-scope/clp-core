# -- Project information -------------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "CLP"
# NOTE: We don't include a period after "Inc" since the theme adds one already.
copyright = "2021-2024 YScope Inc"

# -- General configuration -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    "myst_parser",
    "sphinx_copybutton",
    "sphinx_design",
    "sphinx.ext.autodoc",
    "sphinx.ext.viewcode",
    "sphinxcontrib.mermaid",
]

# -- MyST extensions -----------------------------------------------------------
# https://myst-parser.readthedocs.io/en/stable/syntax/optional.html
myst_enable_extensions = [
    "attrs_block",
    "colon_fence",
]

myst_heading_anchors = 4

# -- Sphinx autodoc options ----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/extensions/autodoc.html#configuration

autoclass_content = "class"
autodoc_class_signature = "separated"
autodoc_typehints = "description"

# -- HTML output options -------------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_favicon = "https://docs.yscope.com/_static/favicon.ico"
html_logo = "https://yscope.com/img/clp-logo.png"
html_title = "CLP"
html_show_copyright = True

html_static_path = ["../src/_static"]

html_theme = "pydata_sphinx_theme"

# -- Theme options -------------------------------------------------------------
# https://pydata-sphinx-theme.readthedocs.io/en/stable/user_guide/layout.html

html_theme_options = {
    "icon_links": [
        {
            "icon": (
                "https://raw.githubusercontent.com/zulip/zulip/main/static/images/logo"
                "/zulip-icon-circle.svg"
            ),
            "name": "CLP on Zulip",
            "url": "https://yscope-clp.zulipchat.com/",
            "type": "url",
        },
    ],
    "footer_start": ["copyright"],
    "footer_center": [],
    "footer_end": ["theme-version"],
    "navbar_start": ["navbar-logo", "version-switcher"],
    "navbar_end": ["navbar-icon-links", "theme-switcher"],
    "primary_sidebar_end": [],
    "secondary_sidebar_items": ["page-toc"],
    "show_prev_next": False,
    "switcher": {
        "json_url": "https://docs.yscope.com/_static/clp-versions.json",
        "version_match": "0.3.0",
    },
    "use_edit_page_button": True,
}

# -- Theme custom CSS and JS ---------------------------------------------------
# https://pydata-sphinx-theme.readthedocs.io/en/stable/user_guide/static_assets.html


def setup(app):
    app.add_css_file("custom.css")
