class LoadError < ScriptError; end

module Kernel

  def load(path)
    raise TypeError unless path.class == String

    if File.exist?(path) && File.extname(path) == ".mrb"
      _load_mrb_file path
    elsif File.exist?(path)
      _load_rb_str File.open(path).read.to_s, path
    else
      raise LoadError, "File not found -- #{path}"
    end

    true
  end

  def require(path)
    raise TypeError unless path.class == String

    # require method can load .rb, .mrb or without-ext filename only.
    filenames = []
    if [".rb", ".mrb"].include? File.extname(path) then
      filenames << path
    else
      filenames << "#{path}.rb"
      filenames << "#{path}.mrb"
    end

    dir = nil
    filename = nil
    if ['/', '.'].include? path[0]
      path0 = filenames.find do |fname|
        File.file?(fname)
      end
    else
      dir = ($LOAD_PATH || []).find do |dir0|
        filename = filenames.find do |fname|
          path0 = File.join dir0, fname
          File.file?(path0)
        end
      end
      path0 = dir && filename ? File.join(dir, filename) : nil
    end

    unless path0 && File.file?(path0)
      raise LoadError, "cannot load such file (bad path or not a file) -- #{path}"
    end

    realpath = File.realpath(path0)

    # already required
    return false if ($" + $__mruby_loading_files__).include?(realpath)

    $__mruby_loading_files__ << realpath
    load realpath
    $" << realpath
    $__mruby_loading_files__.delete realpath

    true
  end
end


$LOAD_PATH ||= []
$LOAD_PATH << '.'
if Object.const_defined?(:ENV)
  $LOAD_PATH.unshift(*ENV['MRBLIB'].split(':')) unless ENV['MRBLIB'].nil?
end
$LOAD_PATH.uniq!

$"                       ||= []
$__mruby_loading_files__ ||= []
