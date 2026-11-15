#ifndef COMPONENTS_FILES_FIXEDPATH_HPP
#define COMPONENTS_FILES_FIXEDPATH_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <components/files/macospath.hpp>

/**
 * \namespace Files
 */
namespace Files
{

    /**
     * \struct Path
     */
    struct FixedPath
    {
        /**
         * \brief Path constructor.
         *
         * \param [in] applicationName - Name of the application
         */
        FixedPath(const std::string& applicationName)
            : mPath(applicationName + "/")
            , mUserConfigPath(mPath.getUserConfigPath())
            , mUserDataPath(mPath.getUserDataPath())
            , mGlobalConfigPath(mPath.getGlobalConfigPath())
            , mLocalPath(mPath.getLocalPath())
            , mGlobalDataPath(mPath.getGlobalDataPath())
            , mCachePath(mPath.getCachePath())
        {
        }

        /**
         * \brief Return path pointing to the user local configuration directory.
         */
        const std::filesystem::path& getUserConfigPath() const { return mUserConfigPath; }

        const std::filesystem::path& getUserDataPath() const { return mUserDataPath; }

        /**
         * \brief Return path pointing to the global (system) configuration directory.
         */
        const std::filesystem::path& getGlobalConfigPath() const { return mGlobalConfigPath; }

        /**
         * \brief Return path pointing to the directory where application was started.
         */
        const std::filesystem::path& getLocalPath() const { return mLocalPath; }

        std::vector<std::filesystem::path> getInstallPaths() const { return mPath.getInstallPaths(); }

        const std::filesystem::path& getGlobalDataPath() const { return mGlobalDataPath; }

        const std::filesystem::path& getCachePath() const { return mCachePath; }

    private:
        MacOsPath mPath;

        std::filesystem::path mUserConfigPath; /**< User path  */
        std::filesystem::path mUserDataPath;
        std::filesystem::path mGlobalConfigPath; /**< Global path */
        std::filesystem::path mLocalPath; /**< It is the same directory where application was run */

        std::filesystem::path mGlobalDataPath; /**< Global application data path */

        std::filesystem::path mCachePath;
    };

} /* namespace Files */

#endif /* COMPONENTS_FILES_FIXEDPATH_HPP */
