#include "muggin.h"
#include "str.h"
#include <stdlib.h>
#include <string.h>

void *default_alloc(Alloc *a, size_t s) {
  return malloc(s);
}

void default_free(Alloc *a, void *ptr) {
  free(ptr);
}

Alloc a = {
  .alloc = default_alloc,
  .free = default_free
};

static int fails = 0;
static int success = 0;


static char *html = NULL;
static size_t html_size = 0;
static FILE *to;

void mug_html(str t, const char *expected) {
  if(html_size) {
    bzero(html, html_size);
  }
  if(!t.len) {
    fprintf(stderr, "\n❌ template is empty");
    goto fail;
  }

  m_Template *tpl = muggin_parse(&a, t);
  if(!tpl) {
    fprintf(stderr, "\n❌ template parse failure:"); //FIXME: pass error out
    goto fail;
  }
  rewind(to);
  muggin_print_html(to, tpl);
  if(strncmp(html, "<!DOCTYPE html>\n", 16) != 0) {
    fprintf(stderr, "\n❌ missing doctype in rendered output");
    goto fail;
  }
  char *actual = &html[16]; // skip doctype
  if(strncmp(actual, expected, strlen(expected)) != 0) {
    fprintf(stderr, "\n❌ actual != expected:"
            "\n expected: %s"
            "\n   actual: %s", expected, actual);
    //FIXME: show position where difference happens
    goto fail;

  }
  printf("✅ ");
  success++;
  return;

 fail:
  fails++;

}

void mug_html_file(const char *file, const char *expected) {
  str t = str_from_file(&a, file);
  mug_html(t, expected);
}

#define TEST(tpl, out) mug_html(str_constant(tpl), out)

int main(int argc, char **argv) {
  to = open_memstream(&html, &html_size);
  mug_html_file("test/simple.mug", "<div id=\"app\" class=\"simple\"><a id=\"home\">this is a link</a><div>inner div</div></div>");
  TEST("div with content", "<div>with content</div>");
  TEST("div#foo bar", "<div id=\"foo\">bar</div>");
  TEST("div\n  | text content", "<div>text content</div>");
  TEST("button(onclick=\"alert('foo')\") click me", "<button onclick=\"alert('foo')\">click me</button>");
  TEST("input(name=foo)", "<input name=\"foo\">");
  TEST("svg(width=50 height=20 )\n  g.group", "<svg width=\"50\" height=\"20\"><g class=\"group\"></g></svg>");
  TEST("img(src=my-image.png\n    alt=\"a nice image\")", "<img src=\"my-image.png\" alt=\"a nice image\">");
  return fails == 0 ? 0 : 1;
}

void log_error(const char *error) {
  fprintf(stderr, "[ERROR] %s\n",  error);
}
void log_notice(const char *notice) {
  fprintf(stdout, "[NOTICE] %s\n", notice);
}
