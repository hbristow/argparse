#ifndef ARGPARSE_HPP_
#define ARGPARSE_HPP_

#if __cplusplus >= 201103L
#include <unordered_map>
typedef std::unordered_map<std::string, size_t> IndexMap;
#else
#include <map>
typedef std::map<std::string, size_t> IndexMap;
#endif
#include <string>
#include <vector>
#include <typeinfo>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cassert>
#include <algorithm>

std::string upper(const std::string& in) {
  std::string out(in);
  std::transform(out.begin(), out.end(), out.begin(), ::toupper);
  return out;
}

/*! @class ArgumentParser
 *  @brief A simple command-line argument parser based on the design of
 *  python's parser of the same name.
 *
 *  ArgumentParser is a simple C++ class that can parse arguments from
 *  the command-line or any array of strings. The syntax is familiar to
 *  anyone who has used python's ArgumentParser:
 *  \code
 *    // create a parser and add the options 
 *    ArgumentParser parser;
 *    parser.addArgument("-n", "--name");
 *    parser.addArgument("--inputs", '+');
 *
 *    // parse the command-line arguments
 *    parser.parse(argc, argv);
 *
 *    // get the inputs and iterate over them
 *    string name = parser.retrieve("name");
 *    vector<string> inputs = parser.retrieve<vector<string>>("inputs");
 *  \endcode
 *
 */
class ArgumentParser {
private:
  // --------------------------------------------------------------------------
  // Type-erasure internal storage
  // --------------------------------------------------------------------------
  class Any {
  public:
    // constructor
    Any()  : content(0) {}
    // destructor
    ~Any() { delete content; } 
    // INWARD CONVERSIONS
    Any(const Any& other) : content(other.content ? other.content->clone() : 0) {}
    template <typename ValueType>
    Any(const ValueType& other) : content(new Holder<ValueType>(other)) {}
    Any& swap(Any& other) {
      std::swap(content, other.content);
      return *this;
    }
    Any& operator=(const Any& rhs) {
      Any tmp(rhs);
      return swap(tmp);
    }
    template <typename ValueType>
    Any& operator=(const ValueType& rhs) {
      Any tmp(rhs);
      return swap(tmp);
    }
    // OUTWARD CONVERSIONS
    template <typename ValueType>
    const ValueType* toPtr() const {
      return content->type_info() == typeid(ValueType) ? &static_cast<Holder<ValueType> *>(content)->held_ : 0;
    }
    template <typename ValueType>
    ValueType castTo() {
      const ValueType* result = toPtr<ValueType>();
      return result ? *result : throw std::bad_cast();
    }
  private:
    // Inner placeholder interface
    class PlaceHolder {
      public:
        virtual ~PlaceHolder() {}
        virtual const std::type_info& type_info() const = 0;
        virtual PlaceHolder* clone() const = 0;
    };
    // Inner template concrete instantiation of PlaceHolder
    template <typename ValueType>
    class Holder : public PlaceHolder {
    public:
      const ValueType held_;
      Holder(const ValueType& value) : held_(value) {}
      virtual const std::type_info& type_info() const {
        return typeid(ValueType);
      }
      virtual PlaceHolder* clone() const {
        return new Holder(held_);
      }
    };
    PlaceHolder* content;
  };

  // --------------------------------------------------------------------------
  // Argument
  // --------------------------------------------------------------------------
  struct Argument {
    Argument(const std::string& _short_name, const std::string& _name, bool _optional, char nargs)
        : short_name(_short_name), name(_name), optional(_optional) {
      if (nargs == '+' || nargs == '*') { 
        variable_nargs = nargs; 
        fixed = false;
      } else {
        fixed_nargs = nargs;
        fixed = true;
      }
    }
    std::string short_name;
    std::string name;
    bool optional;
    union {
      size_t fixed_nargs;
      char variable_nargs;
    };
    bool fixed;
    std::string toString() const {
      std::ostringstream s;
      std::string uname = name.empty() ? upper(short_name) : upper(name);
      if (optional) s << "[";
      if (!name.empty()) { s << "--" << name; } else { s << "-" << short_name; }
      if (fixed) {
        size_t N = std::min((size_t)3, fixed_nargs);
        for (size_t n = 0; n < N; ++n) s << " " << uname;
        if (N < fixed_nargs) s << " ...";
      }
      if (!fixed) {
        s << " ";
        if (variable_nargs == '*') s << "[";
        s << uname << " ";
        if (variable_nargs == '+') s << "[";
        s << uname << "...]";
      }
      if (optional) s << "]";
      return s.str();
    }
  };

  void insertArgument(const Argument& arg) {
    size_t N = arguments_.size();
    arguments_.push_back(arg);
    if (arg.fixed)  variables_.push_back(std::string());
    if (!arg.fixed) variables_.push_back(std::vector<std::string>());
    if (!arg.short_name.empty()) index_[arg.short_name] = N;
    if (!arg.name.empty())       index_[arg.name] = N;
  }

  // --------------------------------------------------------------------------
  // Member variables
  // --------------------------------------------------------------------------
  IndexMap index_; 
  bool ignore_first_;
  std::string app_name_;
  std::string final_name_;
  std::vector<Argument> arguments_;
  std::vector<Any> variables_;
public:
  // --------------------------------------------------------------------------
  // addArgument
  // --------------------------------------------------------------------------
  void appName(const std::string& name) { app_name_ = name; }
  void addArgument(const std::string& name, char nargs=0, bool optional=true) {
    if (name.size() > 2) {
      Argument arg("", sanitize(name), optional, nargs);
      insertArgument(arg);
    } else {
      Argument arg(sanitize(name), "", optional, nargs);
      insertArgument(arg);
    }
  }
  void addArgument(const std::string& short_name, const std::string& name, char nargs=0, bool optional=true) {
    Argument arg(sanitize(short_name), sanitize(name), optional, nargs);
    insertArgument(arg);
  }
  void addFinalArgument(const std::string& name, char nargs=1, bool optional=false) {
    final_name_ = name;
    Argument arg("", name, optional, nargs);
    insertArgument(arg);
  }
  void ignoreFirstArgument(bool ignore_first) { ignore_first_ = ignore_first; }

  std::string sanitize(const std::string& name) {
    // make sure single character arguments have '-'
    if (name.size() == 2) {
      // short name
      if (name[0] == '-') {
        return name.substr(1);
      } else {
        throw std::invalid_argument(std::string("Invalid argument: ").append(name)
          .append(". Short names must begin with '-'").c_str());
      }
    }
    if (name.size() > 2) {
      // long name
      if (name[0] == '-' && name[1] == '-') {
        return name.substr(2);
      } else {
        throw std::invalid_argument(std::string("Invalid argument: ").append(name)
          .append(". Multi-character names must begin with '--'").c_str());
      }
    }
    throw std::invalid_argument("Argument specifier has the wrong format");
  }
    

  // --------------------------------------------------------------------------
  // Parse
  // --------------------------------------------------------------------------
  void parse(size_t argc, const char** argv) {
    parse(std::vector<std::string>(argv, argv+argc));
  }

  void parse(const std::vector<std::string>& argv) {
    // check if the app is named
    if (app_name_.empty() && !ignore_first_ && !argv.empty()) app_name_ = argv[0];
  }

  // --------------------------------------------------------------------------
  // Retrieve
  // --------------------------------------------------------------------------
  template <typename T>
  T retrieve(const std::string& name) {
    if (index_.count(name) == 0) throw std::out_of_range("Key not found");
    size_t N = index_[name];
    return variables_[N].castTo<T>();
  }

  // --------------------------------------------------------------------------
  // Properties
  // --------------------------------------------------------------------------
  std::string usage() {
    // premable app name
    std::ostringstream help;
    help << "Usage: " << app_name_ << " ";
    size_t indent = help.str().size();
    size_t linelength = 0;

    // get the required arguments
    for (std::vector<Argument>::iterator it = arguments_.begin(); it != arguments_.end(); ++it) {
      Argument arg = *it;
      if (arg.optional) continue;
      if (arg.name.compare(final_name_) == 0) continue;
      std::string argstr = arg.toString();
      if (argstr.size() + linelength > 80) {
        help << "\n" << std::string(indent, ' ');
        linelength = 0;
      } else {
        linelength += argstr.size();
      }
      help << argstr << " ";
    }

    // get the optional arguments
    for (std::vector<Argument>::iterator it = arguments_.begin(); it != arguments_.end(); ++it) {
      Argument arg = *it;
      if (!arg.optional) continue;
      if (arg.name.compare(final_name_) == 0) continue;
      std::string argstr = arg.toString();
      std::cout << help.str().size()%80 << std::endl;
      if (argstr.size() + linelength > 80) {
        help << "\n" << std::string(indent, ' ');
        linelength = 0;
      } else {
        linelength += argstr.size();
      }
      help << argstr << " ";
    }

    // get the final argument
    if (!final_name_.empty()) {
      Argument arg = arguments_[index_[final_name_]];
      std::string argstr = arg.toString();
      if (argstr.size() + help.str().size()%80 > 80) help << "\n" << std::string(indent, ' ');
      help << argstr;
    }

    return help.str();
  }
  bool empty() const { return index_.empty(); }
  void clear() {
    index_.clear();
    arguments_.clear();
    variables_.clear();
  }
  bool exists(const std::string& name) const {
    return index_.count(name) > 0;
  }
  size_t count(const std::string& name) {
    // check if the name is an argument
    if (index_.count(name) == 0) return 0;
    size_t N = index_[name];
    Argument arg = arguments_[N];
    Any var      = variables_[N];
    // check if the argument is a vector
    if (arg.fixed) { 
      return !var.castTo<std::string>().empty();
    } else {
      return  var.castTo<std::vector<std::string> >().size();
    }
  }
};
#endif
