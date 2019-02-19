class LoadError < ScriptError; end

module Kernel

  def ___file_search( path, exts )

    raise TypeError unless path.class == String

    filenames = [path]
    if not ( exts.include? File.extname(path) ) then
      exts.each do |e| filenames << path+e end
    end

    path0 = nil
    if ['/', '.'].include? path[0] or not $LOAD_PATH
      path0 = filenames.find do |fname| File.file? fname end
    else
      found = false
      filenames.find do |fname|
        $LOAD_PATH.each do |dir0|
          path0 = File.join dir0, fname
          found = File.file? path0
          break if found
        end
        break if found
      end
    end

    unless path0 && File.file?(path0)
      raise LoadError,
            "cannot load such file (bad path or not a file) -- #{path}"
    end

    return File.realpath(path0)
  end

  def load(path)
    raise TypeError unless path.class == String

    # require method can load .rb, .mrb or without-ext filename only.
    realpath = ___file_search( path,  [ ".rb", ".mrb", ".so", ".dylib", ".dll"] )

    if File.extname(realpath) == ".mrb"
      ___load_mrb_file realpath
    elsif File.extname(realpath) == ".rb"
      ___load_rb_str File.open(realpath).read.to_s, realpath
    else
      ___load_shared_file realpath
    end

    true
  end

  def require(path)
    raise TypeError unless path.class == String

    # require method can load .rb, .mrb or without-ext filename only.
    realpath = ___file_search( path,  [".rb", ".mrb"] )

    # already required
    return false if ($" + $__mruby_loading_files__).include?(realpath)

    $__mruby_loading_files__ << realpath
    if File.extname(realpath) == ".mrb"
      ___load_mrb_file realpath
    else
      ___load_rb_str File.open(realpath).read.to_s, realpath
    end
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
