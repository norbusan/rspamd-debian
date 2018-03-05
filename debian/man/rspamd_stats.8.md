% RSPAMD_STATS(8)
% Vsevolod Stakhov
% March 5, 2018

# NAME

rspamd_stats - analyze Rspamd rules by parsing log files

# SYNOPSIS

rspamd_stats [*options*] [*--symbol=SYM1* [*--symbol=SYM2*...]] [*--log file*]

# DESCRIPTION

rspamd_stats will read the given log file (or standard input) and provide
statistics for the specified symbols:

    Symbol: BAYES_SPAM (weight 3.763) (381985 hits, 26.827%)
    Ham hits: 184557 (48.315%), total ham: 1095487 (ham with BAYES_SPAM: 16.847%)
    Spam hits: 15134 (3.962%), total spam: 16688 (spam with BAYES_SPAM: 90.688%)
    Junk hits: 182294 (47.723%), total junk: 311699 (junk with BAYES_SPAM: 58.484%)
    Spam changes (ham/junk -> spam): 7026 (1.839%), total percentage (changes / spam hits): 42.102%
    Junk changes (ham -> junk): 95192 (24.920%), total percentage (changes / junk hits): 30.540%

Where there are the following attributes:

* Weight: average score for a symbols

* Total hits: total number of hits and percentage of symbol hits divided
  by total number of messages

* HAM hits: provides the following information about HAM messages with
  the specified symbol (from left to right):

  1.  total symbol hits: number of messages that has this symbol and are
      HAM

  2.  ham percentage: number of symbol hits divided by overall HAM
      messages count

  3.  total ham hits: overall number of HAM messages

  4.  ham with symbol percentage: percentage of number of hits with
      specified symbol in HAM messages divided by total number of
      HAM messages.

* SPAM hits: provides the following information about SPAM messages -
  same as previous but for SPAM class.

* Junk hits: provides the following information about Junk messages -
  same as previous but for JUNK class.

* Spam changes: displays data about how much messages switched their
  class because of the specific symbol weight.

* Junk changes: displays data about how much messages switched their
  class because of the specific symbol weight.

# OPTIONS

\--log
: Specifies log file or directory to read data from. If a directory
  is specified rspamd_stats analyses files in the directory
  including known compressed file types. Number of log files can be
  limited using \--num-logs and \--exclude-logs options. This assumes
  that files in the log directory have newsyslog(8)- or
  logrotate(8)-like name format with numeric indexes. Files without
  indexes (generally it is merely one file) are considered the most
  recent and files with lower indexes are considered newer.

\--reject-score
: Specifies the reject (spam) threshold.

\--junk-score
: Specifies the junk (add header or rewrite subject) threshold.

\--alpha-score
: Specifies the minimum score for a symbol to be considered by this
  script.

\--symbol
: Add symbol or pattern (pcre format) to analyze.

\--num-logs
: If set, limits number of analyzed logfiles in the directory to the
  specified value.

\--exclude-logs
: Number of latest logs to exclude (0 by default).

\--correlations
: Additionally print correlation rate for each symbol displayed.
  This routine calculates merely paired correlations between
  symbols.

\--search-pattern
: Do not process input unless finding the specified regular
  expression. Useful to skip logs to a certain position.

\--exclude
: Exclude log lines if certain symbols are fired (e.g. GTUBE). You
  may specify this option multiple time to skip multiple symbols.

\--start
: Select log entries after this time. Format: "YYYY-MM-DD HH:MM:SS"
  (can be truncated to any desired accuracy). If used with \--end
  select entries between --start and \--end. The omitted date
  defaults to the current date if you supply the time.

\--end
: Select log entries before this time. Format: "YYYY-MM-DD HH:MM:SS"
  (can be truncated to any desired accuracy). If used with
  \--start select entries between \--start and \--end. The omitted date
  defaults to the current date if you supply the time.

\--help
: Print a brief help message and exits.

\--man
: Prints the manual page and exits.
