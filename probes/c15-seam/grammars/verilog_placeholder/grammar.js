/**
 * Minimal placeholder grammar implementing a Verilog subset.
 *
 * Purpose: probes/c15-seam needs a *Verilog family* grammar to prove the
 * SyntaxProvider seam is grammar-agnostic. The contract explicitly allows a
 * minimal/placeholder grammar here, so this file deliberately covers only the
 * constructs the probe exercises: module declarations, net declarations,
 * continuous assignments, always blocks and a small expression language.
 *
 * Authored for this probe (MIT, see LICENSE). It is NOT derived from any
 * external grammar, so no third-party licence obligation applies to it.
 */
module.exports = grammar({
  name: 'verilog_placeholder',

  extras: $ => [/\s+/, $.line_comment],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      $.module_declaration,
      $.net_declaration,
      $.assign_statement,
      $.always_statement,
    ),

    line_comment: _ => token(seq('//', /[^\n]*/)),

    identifier: _ => /[A-Za-z_][A-Za-z0-9_$]*/,

    number: _ => choice(
      /\d+'[bBoOdDhH][0-9a-fA-FxXzZ_]+/,
      /\d+/,
    ),

    module_declaration: $ => seq(
      'module',
      field('name', $.identifier),
      optional($.port_list),
      ';',
      repeat($._item),
      'endmodule',
    ),

    port_list: $ => seq(
      '(',
      optional(seq($.port, repeat(seq(',', $.port)))),
      ')',
    ),

    port: $ => seq(
      optional(choice('input', 'output', 'inout')),
      optional(choice('wire', 'reg')),
      field('name', $.identifier),
    ),

    net_declaration: $ => seq(
      choice('wire', 'reg'),
      field('name', $.identifier),
      repeat(seq(',', field('name', $.identifier))),
      ';',
    ),

    assign_statement: $ => seq('assign', $.expression, ';'),

    always_statement: $ => seq(
      'always',
      optional($.event_control),
      choice($.begin_block, $.assign_statement),
    ),

    event_control: $ => seq(
      '@',
      '(',
      optional(choice('posedge', 'negedge')),
      $.identifier,
      ')',
    ),

    begin_block: $ => seq(
      'begin',
      repeat(choice($.assign_statement, $.net_declaration)),
      'end',
    ),

    expression: $ => choice(
      $.identifier,
      $.number,
      $.paren_expression,
      $.binary_expression,
    ),

    paren_expression: $ => seq('(', $.expression, ')'),

    binary_expression: $ => prec.left(seq(
      $.expression,
      choice('&', '|', '^', '+', '-', '*', '/', '==', '!='),
      $.expression,
    )),
  },
});
