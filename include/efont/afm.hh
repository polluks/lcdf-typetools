#ifndef AFM_HH
#define AFM_HH
#include "metrics.hh"
class AfmParser;
class Slurper;
class Filename;
class ErrorHandler;

struct AfmMetricsXt: public MetricsXt {
  
  Vector<PermString> opening_comments;
  PermString notice;
  PermString encoding_scheme;
  
  PermString kind() const			{ return "AFM"; }
  
};

class AfmReader {
  
  Metrics *_afm;
  AfmMetricsXt *_afm_xt;
  AfmParser &_l;
  ErrorHandler *_errh;
  
  mutable bool _composite_warned;
  mutable bool _metrics_sets_warned;
  mutable int _y_width_warned;
  
  void lwarning(const char *, ...) const;
  void lerror(const char *, ...) const;
  void composite_warning() const;
  void metrics_sets_warning() const;
  void y_width_warning() const;
  void no_match_warning(const char *context = 0) const;
  
  double &fd(int i)				{ return _afm->fd(i); }
  GlyphIndex find_err(PermString, const char *) const;
  
  void read_char_metric_data() const;
  void read_char_metrics() const;
  void read_kerns() const;
  void read_composites() const;
  
  bool read();
  
  AfmReader(AfmParser &, Metrics *, AfmMetricsXt *, ErrorHandler *);
  
 public:
  
  static Metrics *read(const Filename &, ErrorHandler *);
  static Metrics *read(Slurper &, ErrorHandler *);
  
};

#endif
