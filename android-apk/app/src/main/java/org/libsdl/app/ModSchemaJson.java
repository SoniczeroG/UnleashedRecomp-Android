package org.libsdl.app;

import java.io.IOException;

/** JSON.NET-compatible schema syntax, without changing quoted text or error offsets. */
final class ModSchemaJson {
    private ModSchemaJson() {}

    static String normalize(String source) throws IOException {
        char[] text = source.toCharArray();
        if (text.length > 0 && text[0] == '\uFEFF') text[0] = ' ';
        char quote = 0;
        boolean escaped = false;
        // Replace comments with whitespace before looking ahead for trailing commas.
        for (int i = 0; i < text.length; i++) {
            char c = text[i];
            if (quote != 0) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '/' && i + 1 < text.length && text[i + 1] == '/') {
                while (i < text.length && text[i] != '\n' && text[i] != '\r') text[i++] = ' ';
                i--;
            } else if (c == '/' && i + 1 < text.length && text[i + 1] == '*') {
                int start = i;
                text[i++] = ' ';
                text[i++] = ' ';
                while (i + 1 < text.length && !(text[i] == '*' && text[i + 1] == '/')) {
                    if (text[i] != '\n' && text[i] != '\r') text[i] = ' ';
                    i++;
                }
                if (i + 1 >= text.length) throw new LocalizedIOException("error_schema_comment", start);
                text[i++] = ' ';
                text[i] = ' ';
            }
        }
        quote = 0;
        escaped = false;
        for (int i = 0; i < text.length; i++) {
            char c = text[i];
            if (quote != 0) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == ',') {
                int next = i + 1;
                while (next < text.length && Character.isWhitespace(text[next])) next++;
                if (next < text.length && (text[next] == ']' || text[next] == '}')) text[i] = ' ';
            }
        }
        return new String(text);
    }
}
