#include <algorithm>
#include <array>
#include <list>
#include <cassert>
#include <string>

#include "argparse.hpp"
#include "argparse_util.hpp"

namespace argparse {

    /*
     * ArgumentParser
     */

    ArgumentParser::ArgumentParser(std::string description_str, std::ostream& os)
        : description_(description_str)
        , formatter_(new DefaultFormatter())
        , os_(os)
        {
        argument_groups_.push_back(ArgumentGroup("arguments:"));
    }

    ArgumentParser& ArgumentParser::prog(std::string prog_name, bool basename_only) {
        if (basename_only) {
            prog_ = basename(prog_name);
        } else {
            prog_ = prog_name;
        }
        return *this;
    }

    ArgumentParser& ArgumentParser::epilog(std::string epilog_str) {
        epilog_ = epilog_str;
        return *this;
    }

    ArgumentGroup& ArgumentParser::add_argument_group(std::string description_str) {
        argument_groups_.push_back(ArgumentGroup(description_str));
        return argument_groups_[argument_groups_.size() - 1];
    }


    std::vector<std::shared_ptr<Argument>> ArgumentParser::parse_args(int argc, const char** argv) {
        std::vector<std::string> arg_strs;
        for (int i = 1; i < argc; ++i) {
            arg_strs.push_back(argv[i]);
        }

        return parse_args(arg_strs);
    }
    
    std::vector<std::shared_ptr<Argument>> ArgumentParser::parse_args(std::vector<std::string> arg_strs) {
        //Reset all the defaults
        for (const auto& group : argument_groups()) {
            for (const auto& arg : group.arguments()) {
                if (arg->default_set()) {
                    arg->set_dest_to_default();
                }
            }
        }

        //Create a look-up of expected argument strings and positional arguments
        std::map<std::string,std::shared_ptr<Argument>> str_to_option_arg;
        std::list<std::shared_ptr<Argument>> positional_args;
        for (const auto& group : argument_groups()) {
            for (const auto& arg : group.arguments()) {
                if (arg->positional()) {
                    positional_args.push_back(arg);
                } else {
                    for (const auto& opt : {arg->long_option(), arg->short_option()}) {
                        if (opt.empty()) continue;

                        auto ret = str_to_option_arg.insert(std::make_pair(opt, arg));

                        if (!ret.second) {
                            //Option string already specified
                            std::stringstream ss;
                            ss << "Option string '" << opt << "' maps to multiple options";
                            throw ArgParseError(ss.str());
                        }
                    }
                }
            }
        }

        std::vector<std::shared_ptr<Argument>> specified_arguments;

        //Process the arguments
        for (size_t i = 0; i < arg_strs.size(); i++) {
            auto iter = str_to_option_arg.find(arg_strs[i]);
            if (iter != str_to_option_arg.end()) {
                //Start of an argument
                auto& arg = iter->second;

                specified_arguments.push_back(arg);

                std::string value;
                if (arg->action() == Action::STORE_TRUE) {
                    value = "true";

                } else if (arg->action() == Action::STORE_FALSE) {
                    value = "false";

                } else {
                    assert(arg->action() == Action::STORE);


                    size_t max_values_to_read = 0;
                    size_t min_values_to_read = 0;
                    if (arg->nargs() == '1') {
                        max_values_to_read = 1;
                        min_values_to_read = 1;
                    } else if (arg->nargs() == '?') {
                        max_values_to_read = 1;
                        min_values_to_read = 0;
                    } else if (arg->nargs() == '*') {
                        max_values_to_read = std::numeric_limits<size_t>::max();
                        min_values_to_read = 0;
                    } else {
                        assert (arg->nargs() == '+');
                        max_values_to_read = std::numeric_limits<size_t>::max();
                        min_values_to_read = 1;
                    }

                    std::vector<std::string> values;
                    size_t nargs_read;
                    for (nargs_read = 0; nargs_read < max_values_to_read; ++nargs_read) {
                        size_t next_idx = i + 1 + nargs_read;
                        if (next_idx >= arg_strs.size()) {
                            std::stringstream msg;
                            msg << "Missing expected argument for '" << arg_strs[i] << "'";
                            throw ArgParseError(msg.str());

                        }
                        std::string str = arg_strs[next_idx];


                        if (is_argument(str)) break;

                        values.push_back(str);
                    }

                    if (nargs_read < min_values_to_read) {
                        std::stringstream msg;
                        msg << "Expected at least " << min_values_to_read << " values for argument '" << arg_strs[i] << "'";
                        throw ArgParseError(msg.str().c_str());
                    }
                    assert (nargs_read <= max_values_to_read);

                    //Set the option values appropriately
                    if (arg->nargs() == '1') {
                        assert(nargs_read == 1);
                        assert(values.size() == 1);
                        arg->set_dest_to_value_from_str(values[0]); 
                    } else {
                        //Multiple values
                        assert(false); //TODO: implement
                    }

                    i += nargs_read; //Skip over the values
                }

            } else {
                if (positional_args.empty()) {
                    //Unrecognized
                    std::stringstream ss;
                    ss << "Unexpected command-line argument '" << arg_strs[i] << "'";
                    throw ArgParseError(ss.str().c_str());
                } else {
                    //Positional argument
                    auto arg = positional_args.front();
                    positional_args.pop_front();

                    auto value = arg_strs[i];
                    specified_arguments.push_back(arg);
                }
            }
        }

        //Missing positionals?
        for(const auto& remaining_positional : positional_args) {
            std::stringstream ss;
            ss << "Missing required positional argument: " << remaining_positional->long_option();
            throw ArgParseError(ss.str());
        }

        return specified_arguments;
    }

    void ArgumentParser::print_help() {
        formatter_->set_parser(this);
        os_ << formatter_->format_usage();
        os_ << formatter_->format_description();
        os_ << formatter_->format_arguments();
        os_ << formatter_->format_epilog();
    }

    std::string ArgumentParser::prog() const { return prog_; }
    std::string ArgumentParser::description() const { return description_; }
    std::string ArgumentParser::epilog() const { return epilog_; }
    std::vector<ArgumentGroup> ArgumentParser::argument_groups() const { return argument_groups_; }

    /*
     * ArgumentGroup
     */
    ArgumentGroup::ArgumentGroup(std::string name_str)
        : name_(name_str)
        {}

    ArgumentGroup& ArgumentGroup::epilog(std::string str) {
        epilog_ = str;
        return *this;
    }
    std::string ArgumentGroup::name() const { return name_; }
    std::string ArgumentGroup::epilog() const { return epilog_; }
    const std::vector<std::shared_ptr<Argument>>& ArgumentGroup::arguments() const { return arguments_; }

    /*
     * Argument
     */
    Argument::Argument(std::string long_opt, std::string short_opt)
        : long_opt_(long_opt)
        , short_opt_(short_opt) {

        if (long_opt_.size() < 1) {
            throw ArgParseError("Argument must be at least one character long");
        }

        auto dashes_name = split_leading_dashes(long_opt_);

        if (dashes_name[0].size() == 1 && !short_opt_.empty()) {
            throw ArgParseError("Long option must be specified before short option");
        } else if (dashes_name[0].size() > 2) {
            throw ArgParseError("More than two dashes in argument name");
        }

        //Set defaults
        metavar_ = toupper(dashes_name[1]);
    }

    Argument& Argument::help(std::string help_str) {
        help_ = help_str;
        return *this;
    }

    Argument& Argument::nargs(char nargs_type) {
        //TODO: nargs > 1 support: '?', '*', '+'
        std::array<char,5> valid_nargs = {'0', '1'};

        auto iter = std::find(valid_nargs.begin(), valid_nargs.end(), nargs_type);
        if (iter == valid_nargs.end()) {
            throw ArgParseError("Invalid argument to nargs (must be one of: " + join(valid_nargs, ", ") + ")");
        }

        //Ensure nargs is consistent with the action
        if (action() == Action::STORE_FALSE && nargs_type != '0') {
            throw ArgParseError("STORE_FALSE action requires nargs to be '0'");
        } else if (action() == Action::STORE_TRUE && nargs_type != '0') {
            throw ArgParseError("STORE_TRUE action requires nargs to be '0'");
        } else if (action() == Action::STORE && nargs_type != '1') {
            throw ArgParseError("STORE action requires nargs to be '1'");
        }

        nargs_ = nargs_type;
        return *this;
    }

    Argument& Argument::metavar(std::string metavar_str) {
        metavar_ = metavar_str;
        return *this;
    }

    Argument& Argument::choices(std::vector<std::string> choice_values) {
        choices_ = choice_values;
        return *this;
    }

    Argument& Argument::action(Action action_type) {
        action_ = action_type;

        if (action_ == Action::STORE_FALSE || action_ == Action::STORE_TRUE) {
            this->nargs('0');
        } else if (action_ == Action::STORE) {
            this->nargs('1');
        } else {
            throw ("Unrecognized argparse action");
        }

        return *this;
    }

    Argument& Argument::required(bool is_required) {
        required_ = is_required;
        return *this;
    }

    Argument& Argument::default_value(const std::string& value) {
        default_value_ = value;
        default_set_ = true;
        return *this;
    }

    Argument& Argument::group_name(std::string grp) {
        group_name_ = grp;
        return *this;
    }

    Argument& Argument::show_in(ShowIn show) {
        show_in_ = show;
        return *this;
    }

    std::string Argument::long_option() const { return long_opt_; }
    std::string Argument::short_option() const { return short_opt_; }
    std::string Argument::help() const { return help_; }
    char Argument::nargs() const { return nargs_; }
    std::string Argument::metavar() const { return metavar_; }
    std::vector<std::string> Argument::choices() const { return choices_; }
    Action Argument::action() const { return action_; }
    std::string Argument::default_value() const { return default_value_; }
    std::string Argument::group_name() const { return group_name_; }
    ShowIn Argument::show_in() const { return show_in_; }
    bool Argument::default_set() const { return default_set_; }

    bool Argument::required() const {
        if(positional()) {
            //Positional arguments are always required
            return true;
        }
        return required_;
    }
    bool Argument::positional() const {
        assert(long_option().size() > 1);
        return long_option()[0] != '-';
    }

} //namespace
